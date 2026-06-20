#!/usr/bin/env bash
# doctor.sh — preflight check that a machine can actually use the GPU with cheatah-gpu.
#
# Verifies the userspace GPU stack and tells you EXACTLY how to fix any gap (usually:
# `scripts/install-deps.sh`). Checks:
#   1. Vulkan loader resolves (libvulkan present)            — required
#   2. a Vulkan-capable device is enumerable (`vulkaninfo`)  — required (this is the driver/ICD)
#   3. `slangc` compiles shaders/hello.slang to valid SPIR-V — required for Slang shaders
#   4. GLFW is present                                        — optional (tests/presentation only)
#
# Exit code 0 = ready for GPU work; non-zero = a required component is missing. This is the
# convention a future `biome doctor` will run after install.
set -uo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || dirname "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")"

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

status=0
echo "cheatah-gpu doctor — checking the GPU userspace stack:"

# 1. Vulkan loader -----------------------------------------------------------------------------
# Robust check: macOS ships its own; else ldconfig, else any libvulkan.so on the usual paths
# (glob in a for-loop so an unmatched pattern doesn't error the way `ls glob1 glob2` does).
loader_present() {
    [ "$(uname -s)" = "Darwin" ] && return 0
    ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so' && return 0
    for f in /usr/lib/libvulkan.so* /usr/lib/*/libvulkan.so* /usr/local/lib/libvulkan.so* /lib/*/libvulkan.so*; do
        [ -e "$f" ] && return 0
    done
    return 1
}
if loader_present; then
    ok "Vulkan loader present"
else
    bad "Vulkan loader (libvulkan) not found — run: scripts/install-deps.sh"; status=1
fi

# 2. A Vulkan device (this is the real driver/ICD test) ----------------------------------------
if have vulkaninfo; then
    if dev=$(vulkaninfo --summary 2>/dev/null | grep -m1 -E 'deviceName' | sed 's/.*= //'); then
        [ -n "$dev" ] && ok "Vulkan device: $dev" || { bad "vulkaninfo ran but enumerated no device — install a GPU driver (Mesa/NVIDIA/AMD)"; status=1; }
    else
        bad "vulkaninfo found no Vulkan device — install/enable a GPU driver (Mesa/NVIDIA/AMD)"; status=1
    fi
else
    warn "vulkaninfo not installed — can't confirm a device. Run: scripts/install-deps.sh"; status=1
fi

# 3. Slang toolchain: one source must compile to BOTH targets (Vulkan SPIR-V + Apple Metal) -----
if have slangc; then
    tmp="$(mktemp -d)"
    if slangc shaders/hello.slang -target spirv -entry main -o "$tmp/hello.spv" >"$tmp/slang.log" 2>&1; then
        if have spirv-val && spirv-val "$tmp/hello.spv" >/dev/null 2>&1; then
            ok "slangc: shaders/hello.slang -> valid SPIR-V (Vulkan)"
        else
            ok "slangc: shaders/hello.slang -> SPIR-V (Vulkan; spirv-val not run)"
        fi
    else
        bad "slangc failed on shaders/hello.slang:"; sed 's/^/      /' "$tmp/slang.log"; status=1
    fi
    # The window-test shader must cross-compile to Vulkan AND Apple from the same Slang source.
    if [ -f shaders/cool.slang ]; then
        slangc shaders/cool.slang -target spirv -o "$tmp/cool.spv"   >>"$tmp/slang.log" 2>&1 \
            && ok "slangc: shaders/cool.slang -> SPIR-V (Vulkan)"  || { bad "shaders/cool.slang -> SPIR-V failed"; status=1; }
        if slangc shaders/cool.slang -target metal -o "$tmp/cool.metal" >>"$tmp/slang.log" 2>&1; then
            ok "slangc: shaders/cool.slang -> Metal (Apple) — same source, both backends"
        else
            warn "slangc could not target Metal here (fine on non-Apple toolchains; Apple builds will)"
        fi
    fi
    rm -rf "$tmp"
else
    bad "slangc not found — needed to build Slang shaders. See: scripts/install-deps.sh"; status=1
fi

# 4. GLFW — optional, test/presentation only ---------------------------------------------------
if pkg-config --exists glfw3 2>/dev/null || ls /usr/lib/*/libglfw* /usr/local/lib/libglfw* >/dev/null 2>&1; then
    ok "GLFW present (used only by the presentation tests)"
else
    warn "GLFW not found — optional; only the presentation tests need it (scripts/install-deps.sh)"
fi

echo
if [ "$status" -eq 0 ]; then
    printf '\033[32mcheatah-gpu: ready for GPU work.\033[0m\n'
else
    printf '\033[31mcheatah-gpu: not ready — fix the ✗ items above (usually: scripts/install-deps.sh).\033[0m\n'
fi
exit "$status"
