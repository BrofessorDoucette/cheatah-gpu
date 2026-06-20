#!/usr/bin/env bash
# sign-modules.sh — write the .sha512 sidecars that mark cheatah-gpu's module headers as VERIFIED
# cheatah modules. This is what makes the extension biome-installable: with the sidecar present,
# purrc resolves `import gpu` / `import gpu.*` on the extension path (CHEATAH_MODULE_PATH, which
# `biome add cheatah-gpu` sets) — so a user with a standard cheatah install never touches git or
# --import-root. The sidecar is the SHA-512 hex of the header; purrc verifies the header matches.
#
# Only the top-level package header needs signing: `import gpu.dispatch` resolves the `gpu` module
# (gpu/gpu.hpp), which includes every submodule. Re-run whenever gpu/gpu.hpp changes; the QA gate
# checks it is in sync.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

for h in gpu/gpu.hpp; do
    sha512sum "$h" | cut -d' ' -f1 > "$h.sha512"
    echo "signed: $h.sha512"
done
