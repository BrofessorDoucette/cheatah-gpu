#!/usr/bin/env bash
# test-biome-install.sh — sandbox the EXACT experience of someone with a standard cheatah install who
# runs `biome add cheatah-gpu`. Nothing from this working tree (build/, .git, dev env vars) is allowed
# to make it falsely pass: we copy ONLY the installable package to a throwaway location (as biome
# fetches it), compile a fresh user project against it with cheatah env vars CLEARED, and the only
# wiring is the extension on CHEATAH_MODULE_PATH — precisely what biome's EXTENSIONS support sets.
#
# If the gpu/gpu.hpp.sha512 sidecar, module layout, or namespaces are wrong, this fails exactly as a
# real user's install would.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
CHEATAH_DIR="${CHEATAH_DIR:-$PWD/../cheatah}"

# The user's installed toolchain (purrc/cheatah). Its stdlib root is BAKED into the binary, so io/etc
# resolve without any CHEATAH_ROOT env — just like a real install.
find_tool() {
    local n="$1"
    for c in release debug asan; do
        [ -x "$CHEATAH_DIR/build/$c/bin/$n" ] && { echo "$CHEATAH_DIR/build/$c/bin/$n"; return 0; }
    done
    command -v "$n" 2>/dev/null
}
PURRC="$(find_tool purrc)"; CHEATAH="$(find_tool cheatah)"
[ -x "$PURRC" ] && [ -x "$CHEATAH" ] || { echo "biome-install: no cheatah toolchain (set CHEATAH_DIR)"; exit 2; }

# 1. Simulate biome fetching cheatah-gpu to a fresh dir OUTSIDE this tree. A consumer gets the `gpu/`
#    package (headers + the .sha512 sidecar) and nothing else — copy exactly that.
INSTALL="$(mktemp -d)"; PROJ="$(mktemp -d)"
trap 'rm -rf "$INSTALL" "$PROJ"' EXIT
cp -r gpu "$INSTALL/gpu"
[ -f "$INSTALL/gpu/gpu.hpp.sha512" ] || { echo "biome-install: the fetched copy has no gpu/gpu.hpp.sha512 — run scripts/sign-modules.sh"; exit 1; }

# 2. A brand-new user project that just imports the package.
mkdir -p "$PROJ/src"
cat > "$PROJ/src/main.purr" <<'PURR'
# Seconds after `biome add cheatah-gpu`. This user never saw a git repo.
import io
import gpu
import gpu.dispatch as dispatch

io.print("hello from the GPU on the", gpu.active_backend_name(), "backend")
let groups = dispatch.group_count_1d(1000000, 256)
if groups == 3907 {
    io.print("RESULT: PASS")
} else {
    io.print("RESULT: FAIL", groups)
}
PURR

# 3. Compile from the user's project with a CLEAN environment: every cheatah env var cleared, and the
#    ONLY wiring is CHEATAH_MODULE_PATH -> the fetched package (what cheatah_add_program EXTENSIONS does).
clean_env=(env -u CHEATAH_ROOT -u CHEATAH_LIB_DIR -u CHEATAH_TRUST -u CHEATAH_DIR
           CHEATAH_MODULE_PATH="$INSTALL")
if ! out="$(cd "$PROJ" && "${clean_env[@]}" "$PURRC" src/main.purr -o app.so 2>&1)"; then
    echo "biome-install: FAILED — a fresh user could not compile 'import gpu':"
    echo "$out" | sed 's/^/    /'
    exit 1
fi
run="$("$CHEATAH" "$PROJ/app.so" 2>&1)"
echo "$run" | sed 's/^/    /'
echo "$run" | grep -q "RESULT: PASS" \
    || { echo "biome-install: FAILED — the user program did not pass"; exit 1; }
echo "biome-install: PASS — fresh project, package fetched to a throwaway dir on CHEATAH_MODULE_PATH, clean env."
