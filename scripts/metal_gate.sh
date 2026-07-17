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

# 1. metal-cpp (Apple's official C++ Metal bindings — you provide it; NO third-party download) -----
# Search: $CHEATAH_GPU_METAL_CPP, then the in-repo vendored copy third_party/metal-cpp. Get metal-cpp
# from Apple at https://developer.apple.com/metal/cpp/ (MIT-licensed) and point CHEATAH_GPU_METAL_CPP
# at it, or vendor it at third_party/metal-cpp/. We never clone it from a mirror.
MCPP="${CHEATAH_GPU_METAL_CPP:-$PWD/third_party/metal-cpp}"
if [ ! -f "$MCPP/Metal/Metal.hpp" ]; then
    fail "metal-cpp not found at '$MCPP'. Download it from https://developer.apple.com/metal/cpp/ and
    set CHEATAH_GPU_METAL_CPP=/path/to/metal-cpp, or vendor it at third_party/metal-cpp/ (needs
    Metal/Metal.hpp + Foundation/Foundation.hpp)."
fi

TEST="tests/metal/metal_compute_test.cpp"
MLTEST="tests/metal/metal_multiline_test.cpp"   # mtl.* calls with multi-line arguments
TXTEST="tests/metal/metal_texture_test.cpp"     # texture -> render-pass clear -> readback (graphics)
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
    # mtl.* calls with multi-line arguments, on the real GPU.
    $CXX $CF "$MLTEST" "$IMPL" $FW -o "$W/metal_real_ml" >"$W/real_ml.log" 2>&1 \
        || { cat "$W/real_ml.log"; fail "real Metal multi-line build"; }
    "$W/metal_real_ml" | sed 's/^/    /' || fail "real Metal multi-line run"
    "$W/metal_real_ml" | grep -q "RESULT: PASS" || fail "real Metal multi-line did not pass"
    # The graphics half: texture -> render-pass clear -> readback, on the real GPU.
    $CXX $CF "$TXTEST" "$IMPL" $FW -o "$W/metal_real_tx" >"$W/real_tx.log" 2>&1 \
        || { cat "$W/real_tx.log"; fail "real Metal texture build"; }
    "$W/metal_real_tx" | sed 's/^/    /' || fail "real Metal texture run"
    "$W/metal_real_tx" | grep -q "RESULT: PASS" || fail "real Metal texture/clear/readback did not pass"
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

    # mtl.* calls with multi-line arguments (verbose GPU code), under ASan.
    bold "Building + running the mtl.* multi-line-argument test under ASan…"
    $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -fsanitize=address,undefined -fno-sanitize=function \
        -g -O1 $EMU "$MLTEST" -o "$W/metal_ml" >"$W/ml_build.log" 2>&1 || { cat "$W/ml_build.log"; fail "multi-line ASan build"; }
    ASAN_OPTIONS=detect_leaks=1 "$W/metal_ml" | sed 's/^/    /' || fail "multi-line ASan run"

    # The GRAPHICS half — texture, render-pass clear, readback — under ASan. This is what lets a
    # consumer's offscreen render tier run on Metal off-Apple, byte-identical to Vulkan.
    bold "Building + running the emulated Metal texture/clear/readback test under ASan + UBSan…"
    $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -fsanitize=address,undefined -fno-sanitize=function \
        -g -O1 $EMU "$TXTEST" -o "$W/metal_tx" >"$W/tx_build.log" 2>&1 || { cat "$W/tx_build.log"; fail "texture ASan build"; }
    ASAN_OPTIONS=detect_leaks=1 "$W/metal_tx" | sed 's/^/    /' || fail "texture ASan run"
    ASAN_OPTIONS=detect_leaks=1 "$W/metal_tx" | grep -q "RESULT: PASS" || fail "texture/clear/readback did not pass"

    # Valgrind memcheck (no definite/indirect leaks) + Helgrind (no races).
    if command -v valgrind >/dev/null 2>&1; then
        bold "Building + running under Valgrind memcheck…"
        $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -g -O0 $EMU "$TEST" -o "$W/metal_vg" >"$W/vg_build.log" 2>&1 \
            || { cat "$W/vg_build.log"; fail "Valgrind build"; }
        valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite,indirect \
            "$W/metal_vg" >"$W/vg.log" 2>&1 || { sed 's/^/    /' "$W/vg.log"; fail "Valgrind (leaks/errors)"; }
        # The texture path too — the render objects are the newest allocation sites.
        $CXX $CF -DCHEATAH_GPU_METAL_LEAKCHECK=1 -g -O0 $EMU "$TXTEST" -o "$W/metal_tx_vg" >"$W/tx_vg_build.log" 2>&1 \
            || { cat "$W/tx_vg_build.log"; fail "Valgrind texture build"; }
        valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite,indirect \
            "$W/metal_tx_vg" >"$W/tx_vg.log" 2>&1 || { sed 's/^/    /' "$W/tx_vg.log"; fail "Valgrind texture (leaks/errors)"; }
        bold "Running under Helgrind (race detection)…"
        valgrind --tool=helgrind --error-exitcode=1 "$W/metal_vg" >"$W/hg.log" 2>&1 \
            || { sed 's/^/    /' "$W/hg.log"; fail "Helgrind (data race)"; }
    else
        bold "valgrind not found — skipping Valgrind + Helgrind (ASan still ran)."
    fi
fi

# 4. Pure-cheatah Metal system tests: drive the compute pipeline FROM CHEATAH (tokens through the
#    handles.hpp model; constants via `import gpu.metal`). Off Apple the module links the emulator
#    objects built here, so the same .purr runs on the software device; on Apple it links the real
#    frameworks and the embedded MSL runs on the GPU.
shopt -s nullglob
mtests=(systests/metal/test_*.purr)
if [ ${#mtests[@]} -gt 0 ]; then
    CHEATAH_DIR="${CHEATAH_DIR:-$PWD/../cheatah}"
    PURRC=""; CHEATAH=""
    for c in release debug asan; do
        [ -z "$PURRC" ]   && [ -x "$CHEATAH_DIR/build/$c/bin/purrc" ]   && PURRC="$CHEATAH_DIR/build/$c/bin/purrc"
        [ -z "$CHEATAH" ] && [ -x "$CHEATAH_DIR/build/$c/bin/cheatah" ] && CHEATAH="$CHEATAH_DIR/build/$c/bin/cheatah"
    done
    # The signed gpu umbrella includes the Vulkan surface too, so resolve the newest SDK's headers
    # the same way vulkan_gate.sh does (the distro's vulkan_core.h is often too old).
    SDK_INC="$(ls -d "$HOME"/Tools/vulkan-sdk/*/x86_64/include "$HOME"/VulkanSDK/*/x86_64/include "$HOME"/Tools/vulkan-sdk/*/macOS/include "$HOME"/VulkanSDK/*/macOS/include 2>/dev/null | sort -V | tail -1)"
    if [ -x "$PURRC" ] && [ -x "$CHEATAH" ] && [ -n "$SDK_INC" ]; then
        bold "Running pure-cheatah Metal system tests…"
        PWRK="$(mktemp -d)"; trap 'rm -rf "$W" "$PWRK"' EXIT  # extend the earlier trap: keep both cleanups
        MCF=(--cxxflag -fblocks --cxxflag -include --cxxflag cmath --cxxflag "-I$MCPP" --cxxflag "-I$PWD")
        MLNK=()
        if [ "$(uname -s)" = "Darwin" ]; then
            MLNK=(--link "-framework" --link "Metal" --link "-framework" --link "Foundation" \
                  --link "-framework" --link "QuartzCore")
        else
            # The emulator objects the module links against (same TUs the C++ tests use).
            MCF+=(--cxxflag "-Igpu/metal/shim")
            $CXX -std=c++20 -fPIC -fblocks -include cmath -I"$MCPP" -Igpu/metal/shim -I"$PWD" -O1 -c \
                gpu/metal/emulated/emulated_metal.cpp -o "$PWRK/emu.o" >"$PWRK/emu_build.log" 2>&1 \
                || { cat "$PWRK/emu_build.log"; fail "emulator object build for systests"; }
            $CXX -std=c++20 -fPIC -fblocks -include cmath -I"$MCPP" -Igpu/metal/shim -I"$PWD" -O1 -c \
                gpu/metal/emulated/block_stubs.cpp -o "$PWRK/stubs.o" >>"$PWRK/emu_build.log" 2>&1 \
                || { cat "$PWRK/emu_build.log"; fail "block-stub object build for systests"; }
            ar rcs "$PWRK/emu.a" "$PWRK/emu.o" "$PWRK/stubs.o"
            MLNK=(--link "$PWRK/emu.a")
        fi
        for t in "${mtests[@]}"; do
            nm="$(basename "$t" .purr)"
            CPATH="$SDK_INC" "$PURRC" --import-root "$PWD" "$t" -o "$PWRK/$nm.so" "${MCF[@]}" "${MLNK[@]}" \
                >"$PWRK/$nm.log" 2>&1 || { sed 's/^/    /' "$PWRK/$nm.log"; fail "compile $t"; }
            out="$("$CHEATAH" "$PWRK/$nm.so" 2>&1)"; echo "$out" | sed 's/^/    /'
            echo "$out" | grep -q "RESULT: PASS" || fail "$t did not pass"
        done
    else
        bold "Skipping pure-cheatah Metal system tests (no cheatah toolchain at $CHEATAH_DIR / no Vulkan SDK include)."
    fi
fi

# 5. Backend auto-resolution: force the WRONG backend for THIS OS and prove it switches + warns -----
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
