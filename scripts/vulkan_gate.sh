#!/usr/bin/env bash
# vulkan_gate.sh — the release gate for the GENERATED Vulkan surface. A release MUST pass this (see
# release.sh), so we never ship generated Vulkan code without checking it. It enforces:
#
#   1. 100% test coverage — every forwarder in gpu/vulkan/commands.hpp has a generated presence test
#      (the generator emits them 1:1; counts must match).
#   2. The Vulkan tests BUILD — proving every forwarder references a real `vk*` entry point — and RUN
#      across every device on this machine. Capability-gated + NON-BLOCKING: a feature the hardware
#      lacks is skipped, never a failure (a reported apiVersion doesn't guarantee every command, e.g.
#      llvmpipe advertises 1.4 but omits some dynamic-state entry points).
#   3. The handwritten behavioral tests pass.
#
# Needs a Vulkan-capable machine (CHEATAH_GPU_BUILD_VULKAN fetches volk + auto-finds the newest SDK).
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
bold() { printf '\n\033[1m[vulkan-gate] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[vulkan-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

# 1. Coverage: one generated TEST_F per command ---------------------------------------------------
# Count COMMANDS by the exact forwarder's doc marker (each command also has a cheatah-friendly
# overload, so `^inline` lines are ~2x), and tests by the per-command TEST_F.
cmds=$(grep -c 'Inline forwarder for' gpu/vulkan/commands.hpp)
chk=$(grep -c 'TEST_F(VulkanPresence' tests/vulkan/generated_presence_checks.cpp)
[ "$cmds" -gt 0 ] || fail "no forwarders in gpu/vulkan/commands.hpp — run the generator"
[ "$cmds" = "$chk" ] || fail "coverage gap: $cmds commands but $chk generated tests — regenerate"
bold "Coverage: 100% — $chk generated presence tests for $cmds commands."

# 2/3. Build + run the Vulkan tests on every device -----------------------------------------------
bold "Configuring + building the Vulkan tests (fetches volk; auto-finds newest SDK)…"
cmake -S . -B build/vk -G Ninja -DCHEATAH_GPU_BUILD_TESTS=ON -DCHEATAH_GPU_BUILD_VULKAN=ON \
    >/tmp/cheatah_gpu_vk_cfg.log 2>&1   || { tail -20 /tmp/cheatah_gpu_vk_cfg.log;   fail "configure (vulkan)"; }
cmake --build build/vk --target cheatah_gpu_vulkan_tests \
    >/tmp/cheatah_gpu_vk_build.log 2>&1 || { tail -30 /tmp/cheatah_gpu_vk_build.log; fail "vulkan test build"; }

bold "Running the Vulkan tests across every device…"
./build/vk/bin/cheatah_gpu_vulkan_tests || fail "vulkan tests"

bold "Vulkan gate PASSED."
exit 0
