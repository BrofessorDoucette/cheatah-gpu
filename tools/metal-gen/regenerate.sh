#!/usr/bin/env bash
# regenerate.sh — refresh the cheatah-facing Metal surface (gpu/metal/types.hpp + symbols.json) from
# the vendored metal-cpp headers. RELEASE-TIME codegen (like tools/vulkan-gen): the output is committed
# so a cheatah-gpu user never runs this. The generator itself is written in cheatah (dogfooding).
#
#   bash tools/metal-gen/regenerate.sh
#
# metal-cpp is fetched + cached the same way scripts/metal_gate.sh does it.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

MCPP="${CHEATAH_GPU_METAL_CPP:-$PWD/build/metal-cpp}"
if [ ! -f "$MCPP/Metal/Metal.hpp" ]; then
    echo "[metal-gen] fetching metal-cpp -> $MCPP"
    mkdir -p "$(dirname "$MCPP")"
    git clone --depth 1 https://github.com/bkaradzic/metal-cpp.git "$MCPP" >/dev/null 2>&1
fi

# The generator reads two concatenated, single-namespace blobs (MTL from Metal/, NS from Foundation/).
# Concatenating keeps the generator to plain io.read_file (no directory walking).
IN="tools/metal-gen/_inputs"
mkdir -p "$IN"
# Metal 3 (namespace MTL) and Metal 4 (namespace MTL4, the next-gen surface — separate, names overlap)
# are split so each lands in its own cheatah namespace; Foundation (NS) joins the Metal 3 surface.
ls "$MCPP"/Metal/*.hpp | grep -v '/MTL4' | xargs cat > "$IN/mtl.hpp"
cat "$MCPP"/Metal/MTL4*.hpp > "$IN/mtl4.hpp"
cat "$MCPP"/Foundation/*.hpp > "$IN/ns.hpp"

CHEATAH_DIR="${CHEATAH_DIR:-$PWD/../cheatah}"
PURRC=""; CHEATAH=""
for c in release debug asan; do
    [ -z "$PURRC" ]   && [ -x "$CHEATAH_DIR/build/$c/bin/purrc" ]   && PURRC="$CHEATAH_DIR/build/$c/bin/purrc"
    [ -z "$CHEATAH" ] && [ -x "$CHEATAH_DIR/build/$c/bin/cheatah" ] && CHEATAH="$CHEATAH_DIR/build/$c/bin/cheatah"
done
[ -x "$PURRC" ] && [ -x "$CHEATAH" ] || { echo "[metal-gen] need purrc + cheatah ($CHEATAH_DIR/build/*/bin)"; exit 1; }

"$PURRC" tools/metal-gen/generate.purr -o /tmp/metal_gen.so
"$CHEATAH" /tmp/metal_gen.so

rm -rf "$IN"
command -v clang-format >/dev/null 2>&1 && clang-format -i gpu/metal/types.hpp || true
echo "[metal-gen] done."
