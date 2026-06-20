#!/usr/bin/env bash
# install-deps.sh — the painless one-shot that gets a machine ready to use the GPU with cheatah-gpu.
#
# This is what `biome add cheatah-gpu` is meant to call (and what a user can run directly). It
# installs the *userspace* GPU stack so a user can immediately compile and run Slang shaders:
#   • the Vulkan loader + headers + validation layers (+ vulkan-tools for `vulkaninfo`)
#   • the Slang compiler `slangc` (bundled in the Vulkan SDK; we point the user at it if absent)
#   • GLFW dev — TEST-ONLY here (cheatah-gpu doesn't own windowing; it's for the presentation tests)
#
# It deliberately does NOT install kernel/GPU drivers (Mesa/NVIDIA/AMD): those are machine-specific
# and unsafe to force. If a working driver/ICD is missing, `scripts/doctor.sh` says exactly what to
# install. Reads the per-platform package lists from cheatah.toml's [system-dependencies] (the
# convention a future `biome install` will consume); falls back to sane defaults below.
#
#   scripts/install-deps.sh            # detect platform + install
#   scripts/install-deps.sh --dry-run  # print what it would do, install nothing
set -uo pipefail

DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1
run() { echo "+ $*"; [ "$DRY" = "1" ] || "$@"; }
have() { command -v "$1" >/dev/null 2>&1; }

echo "cheatah-gpu: provisioning the GPU userspace stack…"

# --- macOS (Homebrew) -------------------------------------------------------------------------
if [ "$(uname -s)" = "Darwin" ]; then
    have brew || { echo "Homebrew not found — install it from https://brew.sh, then re-run."; exit 1; }
    # On Apple, native Metal is preferred; MoltenVK provides the Vulkan path. shader-slang = slangc.
    run brew install molten-vk glfw shader-slang
    echo "macOS: Metal is built in; MoltenVK + Slang installed. Run scripts/doctor.sh to verify."
    exit 0
fi

# --- Windows (roadmap — a side quest, not wired up yet) ----------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        cat <<'EOF'
Windows support is on the roadmap (a side quest for now). For today, install manually:
  • the LunarG Vulkan SDK (loader + slangc):  https://vulkan.lunarg.com/sdk/home
  • a GPU driver from your vendor (NVIDIA / AMD / Intel)
  • GLFW (e.g. via vcpkg) — only if you want the presentation tests
Then run scripts/doctor.sh. A winget/vcpkg one-shot will land later.
EOF
        exit 0 ;;
esac

# --- Linux (apt / dnf / pacman) ---------------------------------------------------------------
SUDO=""; [ "$(id -u)" = "0" ] || SUDO="sudo"

if have apt-get; then
    run $SUDO apt-get update
    # libvulkan-dev pulls the loader+headers; validationlayers + tools for dev; libglfw3-dev test-only.
    run $SUDO apt-get install -y \
        libvulkan-dev vulkan-validationlayers vulkan-tools \
        glslang-tools spirv-tools \
        libglfw3-dev
elif have dnf; then
    run $SUDO dnf install -y \
        vulkan-loader-devel vulkan-validation-layers vulkan-tools \
        glslang spirv-tools \
        glfw-devel
elif have pacman; then
    run $SUDO pacman -S --needed --noconfirm \
        vulkan-icd-loader vulkan-headers vulkan-validation-layers vulkan-tools \
        glslang spirv-tools \
        glfw
else
    echo "No supported package manager (apt/dnf/pacman) found."
    echo "Install manually: the Vulkan loader+headers+validation layers, and (for tests) GLFW dev."
    exit 1
fi

# --- Slang (slangc) — bundled in the Vulkan SDK; not in distro repos ---------------------------
if have slangc; then
    echo "slangc: found ($(command -v slangc))."
else
    cat <<'EOF'

slangc (the Slang shader compiler) was not found. It ships with the Vulkan SDK:
  • Install the LunarG Vulkan SDK:  https://vulkan.lunarg.com/sdk/home  (provides slangc)
  • or grab a Slang release:        https://github.com/shader-slang/slang/releases
Then re-run scripts/doctor.sh.
EOF
fi

echo
echo "Done. Verify everything with:  scripts/doctor.sh"
