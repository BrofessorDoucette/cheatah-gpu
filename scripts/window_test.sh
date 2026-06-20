#!/usr/bin/env bash
# window_test.sh — the STRICTLY OPTIONAL window system test. Draws shaders/cool.slang as an animated
# plasma in a real window, with the animation loop driven from cheatah (tests/window/test_window.purr);
# the window + Vulkan come up in C++ (tests/window/window.cpp). It SKIPS cleanly (exit 0) when there's
# no display, no GLFW build deps, no slangc, or no cheatah toolchain — so it never blocks the headless
# QA gate. Run it directly:  scripts/window_test.sh
#
# Stages: build GLFW from source (cached) -> slangc cool.slang -> SPIR-V -> compile volk + window.cpp
#   -> (1) a C++ smoke renders frames (renderer correctness) -> (2) the cheatah .purr drives the loop
#   via `purrc --link` (the real test: cheatah on top, C++ window underneath).
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
B=build/window
bold() { printf '\n\033[1m[window] %s\033[0m\n' "$*"; }
skip() { printf '\033[33m[window] SKIP: %s\033[0m\n' "$*"; exit 0; }
fail() { printf '\033[31m[window] FAIL: %s\033[0m\n' "$*"; exit 1; }

[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || skip "no display (DISPLAY/WAYLAND_DISPLAY unset)"
command -v slangc >/dev/null 2>&1 || skip "slangc not found (install the Vulkan SDK)"
command -v c++ >/dev/null 2>&1 || skip "no C++ compiler"
SDK_INC="$(ls -d "$HOME"/Tools/vulkan-sdk/*/x86_64/include "$HOME"/VulkanSDK/*/x86_64/include 2>/dev/null | sort -V | tail -1)"
[ -n "$SDK_INC" ] || skip "no Vulkan SDK include found"
CHEATAH_DIR="${CHEATAH_DIR:-$PWD/../cheatah}"
PURRC=""; CHEATAH=""
for c in release debug asan; do  # prefer the first (newest release) — keep, don't overwrite
    [ -z "$PURRC" ]   && [ -x "$CHEATAH_DIR/build/$c/bin/purrc" ]   && PURRC="$CHEATAH_DIR/build/$c/bin/purrc"
    [ -z "$CHEATAH" ] && [ -x "$CHEATAH_DIR/build/$c/bin/cheatah" ] && CHEATAH="$CHEATAH_DIR/build/$c/bin/cheatah"
done

mkdir -p "$B"

# --- GLFW from source (cached; the dev headers for Wayland/X11 must be present) ------------------
if [ ! -f "$B/glfw-prefix/lib/pkgconfig/glfw3.pc" ]; then
    bold "building GLFW from source…"
    rm -rf "$B/glfw-src"
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git "$B/glfw-src" >/tmp/cheatah_glfw.log 2>&1 || skip "could not fetch GLFW"
    cmake -S "$B/glfw-src" -B "$B/glfw-build" -G Ninja -DCMAKE_INSTALL_PREFIX="$PWD/$B/glfw-prefix" \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF >>/tmp/cheatah_glfw.log 2>&1 \
        && cmake --build "$B/glfw-build" >>/tmp/cheatah_glfw.log 2>&1 \
        && cmake --install "$B/glfw-build" >>/tmp/cheatah_glfw.log 2>&1 || { tail -15 /tmp/cheatah_glfw.log; skip "GLFW build failed (missing Wayland/X11 dev headers?)"; }
fi
export PKG_CONFIG_PATH="$PWD/$B/glfw-prefix/lib/pkgconfig"
GLFW_INC="$(pkg-config --cflags-only-I glfw3)"
GLFW_LIBS="$(pkg-config --static --libs glfw3)"

# --- volk source (reuse the Vulkan build's, else fetch) -----------------------------------------
VOLK_SRC="build/vk/_deps/volk-src"
if [ ! -f "$VOLK_SRC/volk.c" ]; then
    git clone --depth 1 --branch 1.4.350 https://github.com/zeux/volk.git "$B/volk-src" >/tmp/cheatah_volk.log 2>&1 || skip "could not fetch volk"
    VOLK_SRC="$B/volk-src"
fi

INC=(-I"$VOLK_SRC" -I"$SDK_INC" $GLFW_INC)

# --- slang -> SPIR-V (Vulkan); the same source also targets Metal on Apple ----------------------
bold "compiling shaders/cool.slang -> SPIR-V…"
slangc shaders/cool.slang -target spirv -o "$B/cool.spv" || fail "slangc"

# --- compile the renderer ------------------------------------------------------------------------
bold "compiling the renderer (volk + window.cpp)…"
c++ -std=c++20 -fPIC -c "$VOLK_SRC/volk.c" -o "$B/volk.o" -I"$SDK_INC" || fail "volk.o"
c++ -std=c++20 -fPIC -c tests/window/window.cpp -o "$B/window.o" "${INC[@]}" -DCOOL_SPV_PATH="\"$PWD/$B/cool.spv\"" || fail "window.o"

# Bundle the renderer into a co-located cheatah MODULE archive: `import window` resolves
# tests/window/window.hpp (via --import-root tests) and purrc auto-links a co-located
# libcheatah_window.a — exactly how a C++-authored stdlib module (e.g. socket) is linked. So the
# .purr just calls window.open/draw/close; no cpp{} escape hatch, no extern "C", no casts.
bold "bundling the window module archive (libcheatah_window.a)…"
ar rcs tests/window/libcheatah_window.a "$B/window.o" "$B/volk.o" || fail "ar"

# --- stage 1: C++ smoke (renderer correctness) --------------------------------------------------
bold "stage 1: C++ smoke renders the plasma…"
c++ -std=c++20 tests/window/smoke.cpp "$B/window.o" "$B/volk.o" -o "$B/smoke" -Itests/window "${INC[@]}" $GLFW_LIBS -ldl || fail "smoke link"
"$B/smoke" || fail "smoke run"

# --- stage 2: cheatah drives the loop, calling the `window` module directly ----------------------
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    rm -f tests/window/libcheatah_window.a
    skip "no cheatah toolchain for the .purr stage (renderer smoke already passed)"
fi
bold "stage 2: cheatah .purr drives the window…"
# purrc resolves `import window` from tests/, auto-links the co-located archive; we only supply the
# external system libs the renderer needs (GLFW + its deps, dl).
LINK=(--link -ldl)
for f in $GLFW_LIBS; do LINK+=(--link "$f"); done
"$PURRC" --import-root tests tests/window/test_window.purr -o "$B/test_window.so" "${LINK[@]}" || { rm -f tests/window/libcheatah_window.a; fail "purrc"; }
out="$("$CHEATAH" "$B/test_window.so" 2>&1)"; echo "$out" | sed 's/^/    /'
rm -f tests/window/libcheatah_window.a
echo "$out" | grep -q "RESULT: PASS" || fail "the cheatah window test did not pass"

bold "window test PASSED — C++ smoke + cheatah-driven Slang plasma."
