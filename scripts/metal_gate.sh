#!/usr/bin/env bash
# metal_gate.sh — the Metal backend gate. Proves the Metal bindings (Apple's metal-cpp) COMPILE and a
# compute kernel RUNS on every platform, by running them on the software-emulated Metal device (the
# Metal analogue of Mesa llvmpipe). On real Apple hardware this same code runs on the GPU; off Apple it
# runs on the CPU emulator (gpu/metal/emulated). It enforces:
#
#   1. The Metal GoogleTest suite (cheatah_gpu_metal_tests — compute, multi-line-argument, and
#      texture/clear/readback suites) runs end-to-end and is bit-correct.
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

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

# 2. The Metal GoogleTest suite (tests/metal -> the cheatah_gpu_metal_tests binary; the mtl:* ctests
#    run the same suites). Built through the CMake presets so this gate and the qa_gate exercise ONE
#    binary: `debug` (plain, what Valgrind/Helgrind can run) and `asan` (ASan + UBSan, minus the
#    `function` check — metal-cpp deliberately calls objc_msgSend through a typed function-pointer
#    cast). On Apple the suite runs on the REAL GPU (Metal compiles the embedded MSL); off Apple it
#    runs on the software-emulated device, leak counter compiled in. gtest's exit code is the truth.
command -v cmake >/dev/null 2>&1 || fail "no cmake (the Metal tests build through the CMake presets)"
if [ "$(uname -s)" = "Darwin" ]; then
    bold "Apple platform detected — the suite runs on REAL Metal on the GPU."
else
    bold "Off Apple — the suite runs on the software-emulated Metal device."
fi

bold "Building + running the Metal GoogleTest suite (debug)…"
cmake --preset debug >"$W/cfg_debug.log" 2>&1 || { tail -20 "$W/cfg_debug.log"; fail "configure (debug)"; }
cmake --build --preset debug --target cheatah_gpu_metal_tests >"$W/build_debug.log" 2>&1 \
    || { tail -30 "$W/build_debug.log"; fail "debug build"; }
DBIN="build/debug/bin/cheatah_gpu_metal_tests"
"$DBIN" | sed 's/^/    /' || fail "Metal GoogleTest suite (debug)"

bold "Building + running the Metal GoogleTest suite under ASan + UBSan…"
cmake --preset asan >"$W/cfg_asan.log" 2>&1 || { tail -20 "$W/cfg_asan.log"; fail "configure (asan)"; }
cmake --build --preset asan --target cheatah_gpu_metal_tests >"$W/build_asan.log" 2>&1 \
    || { tail -30 "$W/build_asan.log"; fail "asan build"; }
# LeakSanitizer does NOT exist in Apple's ASan runtime, and asking for it is not a no-op: the
# runtime aborts at startup with "detect_leaks is not supported on this platform", so every test
# fails before it runs. That is what had this gate red on macOS from 2026-07-25. Ask for leak
# detection only where it exists — the same shape the Valgrind block below already uses, and the
# same one cheatah's own qa_gate uses for Darwin.
ASAN_LEAKS="detect_leaks=1"
if [ "$(uname -s)" = "Darwin" ]; then
    ASAN_LEAKS="detect_leaks=0"
    bold "LeakSanitizer is unavailable on Apple's ASan runtime — running ASan+UBSan without it"
fi
ASAN_OPTIONS="$ASAN_LEAKS" UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
    build/asan/bin/cheatah_gpu_metal_tests | sed 's/^/    /' || fail "Metal GoogleTest suite (ASan/UBSan)"

# Valgrind memcheck (no definite/indirect leaks) + Helgrind (no races) over the SAME debug binary.
if command -v valgrind >/dev/null 2>&1; then
    bold "Running the suite under Valgrind memcheck…"
    valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite,indirect \
        "$DBIN" >"$W/vg.log" 2>&1 || { sed 's/^/    /' "$W/vg.log"; fail "Valgrind (leaks/errors)"; }
    bold "Running the suite under Helgrind (race detection)…"
    valgrind --tool=helgrind --error-exitcode=1 "$DBIN" >"$W/hg.log" 2>&1 \
        || { sed 's/^/    /' "$W/hg.log"; fail "Helgrind (data race)"; }
else
    bold "valgrind not found — skipping Valgrind + Helgrind (ASan still ran)."
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
