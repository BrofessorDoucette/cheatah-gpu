#!/usr/bin/env bash
# release.sh — the ONLY sanctioned way to cut a cheatah-gpu release. It regenerates the GPU surface
# from the vendored registry, then refuses to go further unless BOTH gates pass:
#
#   • scripts/qa_gate.sh     — the host gate (coverage / docs / .purr system tests / ASan / Valgrind /
#                              cppcheck) for the hand-written, host-testable surface.
#   • scripts/vulkan_gate.sh — the generated Vulkan surface is 100% tested and runs on every device.
#
# So a release can NEVER ship the generated Vulkan code without checking it. Run at release time
# (the user: "we run the generation code as part of the release, and fully test it every release").
#
#   scripts/release.sh            # verify the repo is release-ready (does NOT tag)
#   scripts/release.sh vX.Y.Z     # verify, then tag + push + create the GitHub release
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
bold() { printf '\n\033[1m[release] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[release] FAILED: %s\033[0m\n' "$*"; exit 1; }

# 1. Regenerate from the registry and fail if the committed, generated code drifted ---------------
bold "Regenerating the Vulkan surface (code + tests) from vk.xml…"
PURRC="${PURRC:-../cheatah/build/release/bin/purrc}"
CHEATAH="${CHEATAH:-../cheatah/build/release/bin/cheatah}"
[ -x "$PURRC" ] && [ -x "$CHEATAH" ] || fail "cheatah toolchain not found (set PURRC/CHEATAH)"
"$PURRC" tools/vulkan-gen/generate.purr -o /tmp/cheatah_gpu_gen.so >/dev/null 2>&1 || fail "compile generator"
"$CHEATAH" /tmp/cheatah_gpu_gen.so >/dev/null 2>&1 || fail "run generator"
if ! git diff --quiet -- gpu/vulkan tests/vulkan/generated_presence_checks.cpp; then
    fail "generated code is out of date — run tools/vulkan-gen/generate.purr, commit the result, retry"
fi
bold "Generated code is in sync with the registry."

# 2. Host gate ------------------------------------------------------------------------------------
bold "Running the host QA gate…"
bash scripts/qa_gate.sh || fail "qa_gate"

# 3. Vulkan gate (generated surface, every device) ------------------------------------------------
bold "Running the Vulkan gate…"
bash scripts/vulkan_gate.sh || fail "vulkan_gate"

bold "RELEASE-READY ✓ — every gate is green."

# 4. Optional: tag + push + GitHub release --------------------------------------------------------
if [ -n "${1:-}" ]; then
    tag="$1"
    bold "Tagging ${tag}, pushing, and creating the GitHub release…"
    git tag -a "$tag" -m "$tag — all gates green (host + Vulkan, every device)." || fail "git tag"
    git push origin "$tag" --no-verify || fail "push tag"
    case "$tag" in *prealpha*|*alpha*|*beta*|*rc*) pre="--prerelease";; *) pre="";; esac
    gh release create "$tag" $pre --title "$tag" \
        --notes "Release ${tag} — generated Vulkan surface 100% tested across every device; host gate green." \
        || fail "gh release"
    bold "Released ${tag}."
fi
exit 0
