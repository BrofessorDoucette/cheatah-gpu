#!/usr/bin/env bash
# release.sh — the ONLY sanctioned way to cut a cheatah-gpu release. It regenerates BOTH generated
# GPU surfaces from their vendored inputs, then refuses to go further unless EVERY gate passes:
#
#   • scripts/qa_gate.sh     — the host gate (coverage / docs / .purr system tests / ASan / Valgrind /
#                              cppcheck) for the hand-written, host-testable surface.
#   • scripts/metal_gate.sh  — the Metal backend: compute + multi-line + texture pipelines, sanitized
#                              and Valgrind/Helgrind-clean, plus the backend auto-resolution check
#                              (real GPU on Apple, software-emulated device elsewhere).
#   • scripts/vulkan_gate.sh — the generated Vulkan surface is 100% tested and runs on every device.
#
# So a release can NEVER ship generated code (Vulkan OR Metal) without checking it. Run at release
# time (the user: "we run the generation code as part of the release, and fully test it every
# release"). The GitHub release body comes from RELEASE_NOTES.md (required), and the README's
# version line must already name the tag being cut.
#
#   scripts/release.sh            # verify the repo is release-ready (does NOT tag)
#   scripts/release.sh vX.Y.Z     # verify, then tag + push + create the GitHub release
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
bold() { printf '\n\033[1m[release] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[release] FAILED: %s\033[0m\n' "$*"; exit 1; }

# 0. Release collateral: notes + version line (checked FIRST — fail before an hour of gates) ------
[ -f RELEASE_NOTES.md ] || fail "RELEASE_NOTES.md not found — write the release body first (it becomes the gh release notes)"
if [ -n "${1:-}" ]; then
    readme_ver="$(sed -n 's/.*\*\*Version: `\(v[^`]*\)`\*\*.*/\1/p' README.md | head -1)"
    [ -n "$readme_ver" ] || fail "could not find the version line in README.md (expected **Version: \`vX.Y.Z\`**)"
    [ "$readme_ver" = "$1" ] || fail "README.md says version '$readme_ver' but the tag being cut is '$1' — bump the README version line and commit it first"
fi

PURRC="${PURRC:-../cheatah/build/release/bin/purrc}"
CHEATAH="${CHEATAH:-../cheatah/build/release/bin/cheatah}"
[ -x "$PURRC" ] && [ -x "$CHEATAH" ] || fail "cheatah toolchain not found (set PURRC/CHEATAH)"

# 1. Regenerate from the registry and fail if the committed, generated code drifted ---------------
bold "Regenerating the Vulkan surface (code + tests) from vk.xml…"
"$PURRC" tools/vulkan-gen/generate.purr -o /tmp/cheatah_gpu_gen.so >/dev/null 2>&1 || fail "compile Vulkan generator"
"$CHEATAH" /tmp/cheatah_gpu_gen.so >/dev/null 2>&1 || fail "run Vulkan generator"
if ! git diff --quiet -- gpu/vulkan tests/vulkan/generated_presence_checks.cpp; then
    fail "generated Vulkan code is out of date — run tools/vulkan-gen/generate.purr, commit the result, retry"
fi
bold "Generated Vulkan code is in sync with the registry."

# 1b. Same contract for the generated Metal surface (types.hpp + symbols.json from vendored
#     metal-cpp — regenerate.sh is download-free; it uses third_party/metal-cpp).
bold "Regenerating the Metal surface from the vendored metal-cpp…"
bash tools/metal-gen/regenerate.sh >/dev/null 2>&1 || fail "Metal regeneration (tools/metal-gen/regenerate.sh)"
if ! git diff --quiet -- gpu/metal/types.hpp gpu/metal/symbols.json; then
    fail "generated Metal code is out of date — run tools/metal-gen/regenerate.sh, commit the result, retry"
fi
bold "Generated Metal code is in sync with metal-cpp."

# 2. Host gate ------------------------------------------------------------------------------------
bold "Running the host QA gate…"
bash scripts/qa_gate.sh || fail "qa_gate"

# 3. Metal gate (native on Apple, software-emulated device elsewhere) -----------------------------
bold "Running the Metal gate…"
bash scripts/metal_gate.sh || fail "metal_gate"

# 4. Vulkan gate (generated surface, every device) ------------------------------------------------
bold "Running the Vulkan gate…"
bash scripts/vulkan_gate.sh || fail "vulkan_gate"

bold "RELEASE-READY ✓ — every gate is green."

# 5. Optional: tag + push + GitHub release (notes from RELEASE_NOTES.md, tarball attached) --------
if [ -n "${1:-}" ]; then
    tag="$1"
    bold "Tagging ${tag}, pushing, and creating the GitHub release…"
    git tag -a "$tag" -m "$tag — all gates green (host + Metal + Vulkan, every device)." || fail "git tag"
    git push origin "$tag" --no-verify || fail "push tag"
    case "$tag" in *prealpha*|*alpha*|*beta*|*rc*) pre="--prerelease";; *) pre="";; esac
    tarball="/tmp/cheatah-gpu-${tag}.tar.gz"
    git archive --format=tar.gz --prefix="cheatah-gpu-${tag}/" -o "$tarball" "$tag" || fail "git archive"
    gh release create "$tag" $pre --title "$tag" --notes-file RELEASE_NOTES.md || fail "gh release"
    gh release upload "$tag" "$tarball" || fail "gh release upload (source tarball)"
    bold "Released ${tag} (notes from RELEASE_NOTES.md, source tarball attached)."
fi
exit 0
