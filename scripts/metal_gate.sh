#!/usr/bin/env bash
# metal_gate.sh — the Metal backend gate. Proves the Metal bindings (Apple's metal-cpp) COMPILE and a
# compute kernel RUNS on every platform, by running them on the software-emulated Metal device (the
# Metal analogue of Mesa llvmpipe). On real Apple hardware this same code runs on the GPU; off Apple it
# runs on the CPU emulator (gpu/metal/emulated). It enforces:
#
#   1. The emulated compute pipeline runs end-to-end (device -> library -> pipeline -> encode ->
#      dispatch -> read back) and is bit-correct.
#   2. It is leak-clean under Valgrind AND AddressSanitizer, and race-free under Helgrind (cheatah-gpu
#      never threads internally, so there is nothing to race).
#   3. Backend auto-resolution: forcing the WRONG backend for this OS still builds + runs, switched to
#      the correct one, and is NOT silent — it warns (compile-time #warning + a runtime notice).
#
# metal-cpp is fetched + cached (like volk/VMA for Vulkan); nothing here needs an Apple machine.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
bold() { printf '\n\033[1m[metal-gate] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[metal-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

CXX="${CXX:-clang++}"
command -v "$CXX" >/dev/null 2>&1 || fail "no C++ compiler ($CXX)"

# 1. metal-cpp (fetched + cached) -----------------------------------------------------------------
MCPP="${CHEATAH_GPU_METAL_CPP:-$PWD/build/metal-cpp}"
if [ ! -f "$MCPP/Metal/Metal.hpp" ]; then
    bold "Fetching metal-cpp -> $MCPP"
    mkdir -p "$(dirname "$MCPP")"
    git clone --depth 1 https://github.com/bkaradzic/metal-cpp.git "$MCPP" >/tmp/metalcpp_clone.log 2>&1 \
        || { cat /tmp/metalcpp_clone.log; fail "could not fetch metal-cpp"; }
fi

TEST="tests/metal/metal_compute_test.cpp"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

if [ "$(uname -s)" = "Darwin" ]; then
    # ===== REAL Apple hardware: compile the MSL and run the kernels on the actual GPU. ============
    # The SAME test source; here Metal compiles the embedded MSL and dispatches it on-device. metal-cpp
    # comes from the system headers (or the fetched copy); link the Metal + Foundation frameworks.
    bold "Apple platform detected — testing REAL Metal on the GPU."
    CF="-std=c++20 -I$MCPP -I$PWD"
    IMPL="tests/metal/metal_impl.cpp"
    FW="-framework Metal -framework Foundation -framework QuartzCore"
    $CXX $CF "$TEST" "$IMPL" $FW -o "$W/metal_real" >"$W/real_build.log" 2>&1 \
        || { cat "$W/real_build.log"; fail "real Metal build"; }
    "$W/metal_real" | sed 's/^/    /' || fail "real Metal run"
    "$W/metal_real" | grep -q "RESULT: PASS" || fail "real Metal kernels did not pass"
    # Also run it under AddressSanitizer on-device.
    $CXX $CF -fsanitize=address -g -O1 "$TEST" "$IMPL" $FW -o "$W/metal_real_asan" >"$W/real_asan.log" 2>&1 \
        || { cat "$W/real_asan.log"; fail "real Metal ASan build"; }
    "$W/metal_real_asan" >/dev/null || fail "real Metal ASan run"
    bold "Real Metal compute (add_arrays + mul_arrays) ran on the GPU and passed."
else
    # ===== Off Apple: run the SAME test on the software-emulated device (CPU), fully sanitized. ===
    # metal-cpp on a non-Apple OS needs the small Objective-C/CoreFoundation shim + Clang blocks + a
    # forced <cmath> (Apple pulls these transitively). The emulator provides the runtime the shim
    # declares; the embedded MSL is ignored and the registered C++ stand-in kernels run instead.
    CF="-std=c++20 -fblocks -include cmath -I$MCPP -Igpu/metal/shim -I$PWD"
    EMU="gpu/metal/emulated/emulated_metal.cpp gpu/metal/emulated/block_stubs.cpp"

    # AddressSanitizer + UBSan (minus the `function` check: metal-cpp deliberately calls objc_msgSend
    # through a typed function-pointer cast — its documented mechanism, not a bug).
    bold "Building + running the emulated Metal compute test under ASan + UBSan…"
    $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -fsanitize=address,undefined -fno-sanitize=function \
        -g -O1 $EMU "$TEST" -o "$W/metal_asan" >"$W/asan_build.log" 2>&1 || { cat "$W/asan_build.log"; fail "ASan build"; }
    ASAN_OPTIONS=detect_leaks=1 "$W/metal_asan" | sed 's/^/    /' || fail "ASan run (compute or leak check)"

    # Valgrind memcheck (no definite/indirect leaks) + Helgrind (no races).
    if command -v valgrind >/dev/null 2>&1; then
        bold "Building + running under Valgrind memcheck…"
        $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -g -O0 $EMU "$TEST" -o "$W/metal_vg" >"$W/vg_build.log" 2>&1 \
            || { cat "$W/vg_build.log"; fail "Valgrind build"; }
        valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite,indirect \
            "$W/metal_vg" >"$W/vg.log" 2>&1 || { sed 's/^/    /' "$W/vg.log"; fail "Valgrind (leaks/errors)"; }
        bold "Running under Helgrind (race detection)…"
        valgrind --tool=helgrind --error-exitcode=1 "$W/metal_vg" >"$W/hg.log" 2>&1 \
            || { sed 's/^/    /' "$W/hg.log"; fail "Helgrind (data race)"; }
    else
        bold "valgrind not found — skipping Valgrind + Helgrind (ASan still ran)."
    fi
fi

# 4. Backend auto-resolution: force the WRONG backend for THIS OS and prove it switches + warns -----
bold "Verifying backend auto-resolution (forced backend must switch + warn, never silent)…"
cat > "$W/backend_switch.cpp" <<'EOF'
#include "gpu/backend.hpp"
#include <cassert>
#include <cstdio>
namespace g = cheatah::gpu;
int main() {
    // We force Metal at compile time. On Apple that is honored; off Apple it must auto-switch to
    // Vulkan, flag the switch, and print a (dismissable) runtime warning.
    if (g::metal_available) {
        assert(g::active_backend == g::Backend::metal);
        assert(!g::backend_was_switched);
    } else {
        assert(g::requested_backend == g::Backend::metal);
        assert(g::active_backend == g::Backend::vulkan);
        assert(g::backend_was_switched);
        assert(g::warn_backend_selection());   // prints once, returns true
        assert(!g::warn_backend_selection());   // already printed -> quiet
    }
    std::printf("RESULT: PASS\n");
    return 0;
}
EOF
# The forced-wrong-backend build MUST raise the compile-time #warning (never silent).
if ! $CXX -std=c++20 -I"$PWD" -DCHEATAH_GPU_BACKEND_METAL=1 "$W/backend_switch.cpp" -o "$W/backend_switch" \
        2>"$W/bw_build.log"; then
    sed 's/^/    /' "$W/bw_build.log"; fail "backend-switch build"
fi
if ! grep -qi "Switching this build to the Vulkan backend\|Metal only runs on Apple" "$W/bw_build.log" \
   && [ "$(uname -s)" != "Darwin" ]; then
    fail "backend switch was SILENT — expected a compile-time #warning on a non-Apple build"
fi
out="$("$W/backend_switch" 2>"$W/bw_run.err")"; echo "$out" | sed 's/^/    /'
echo "$out" | grep -q "RESULT: PASS" || { sed 's/^/    /' "$W/bw_run.err"; fail "backend-switch run"; }
if [ "$(uname -s)" != "Darwin" ]; then
    grep -qi "WARNING: backend 'metal' was requested" "$W/bw_run.err" \
        || fail "backend switch printed no runtime warning (must not be silent)"
    bold "Backend switch warned at compile time AND runtime: $(grep -o "WARNING:.*instead." "$W/bw_run.err" | head -1)"
fi

bold "Metal gate PASSED."
exit 0
