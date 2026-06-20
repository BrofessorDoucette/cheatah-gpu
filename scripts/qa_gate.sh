#!/usr/bin/env bash
# QA gate for cheatah-gpu — the quality checks that must pass before a push. Invoked by the git
# pre-push hook (.githooks/pre-push) and runnable by hand. Exits non-zero to BLOCK the push.
#
#   1. Unit coverage (hard gate): clang source-based coverage of cheatah_gpu_tests must be 100%
#      lines AND functions over gpu/**/*.hpp; the README coverage table must be committed in sync.
#   2. Doc coverage (hard gate): 100% Javadoc on the public gpu C++ API (scripts/doc_coverage.sh).
#   3. Configure + build (debug).
#   4. System tests (hard gate): every systests/test_*.purr compiles against the gpu package via
#      purrc and runs under the cheatah runtime, finishing with `RESULT: PASS`.
#   5. Unit tests (hard gate): ctest.
#   6. ASan + UBSan (hard gate): build + run the suite under sanitizers.
#   7. Valgrind memcheck (hard gate): run every unit test under Valgrind.
#   8. cppcheck (hard gate): performance + security static analysis.
#
# The toolchain (purrc + cheatah runtime) is the sibling ../cheatah checkout; override with
# CHEATAH_DIR. Skips (discouraged; for fast local iteration): QA_GATE_SKIP_COVERAGE,
# QA_GATE_SKIP_DOCS, QA_GATE_SKIP_ASAN, QA_GATE_SKIP_VALGRIND. QA_GATE_SKIP=1 bypasses everything.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
REPO_ROOT="$(pwd)"
CHEATAH_DIR="${CHEATAH_DIR:-$(cd "$REPO_ROOT/../cheatah" 2>/dev/null && pwd || true)}"

if [ "${QA_GATE_SKIP:-0}" = "1" ]; then
    printf '\n[qa-gate] QA_GATE_SKIP=1 — skipping the QA gate (NOT recommended).\n'; exit 0
fi

bold()  { printf '\n\033[1m[qa-gate] %s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
fail()  { printf '\n\033[31m[qa-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

# 1. Unit coverage: refresh the README table, fail if it drifted, hard-fail unless 100% ----------
if [ "${QA_GATE_SKIP_COVERAGE:-0}" = "1" ]; then
    bold "Skipping coverage stage (QA_GATE_SKIP_COVERAGE=1)."
else
    bold "Measuring unit-test coverage (clang source-based) + refreshing the README table…"
    bash scripts/coverage.sh update-readme >/tmp/cheatah_gpu_coverage.log 2>&1 || { tail -30 /tmp/cheatah_gpu_coverage.log; fail "coverage report"; }
    if ! git diff --quiet -- README.md; then
        printf '\n[qa-gate] The README coverage table is out of date. Updated it to:\n\n'
        git --no-pager diff -- README.md | sed -n '/coverage:start/,/coverage:end/p'
        fail "README coverage table changed — 'git add README.md && git commit', then push again"
    fi
    cat /tmp/cheatah_gpu_coverage.log
    covnums=$(sed -n 's/.*lines [0-9.]*% (\([0-9]*\)\/\([0-9]*\)), functions [0-9.]*% (\([0-9]*\)\/\([0-9]*\)).*/\1 \2 \3 \4/p' /tmp/cheatah_gpu_coverage.log)
    [ -n "$covnums" ] || fail "could not parse the coverage summary (coverage.sh output changed?)"
    read -r lcov_n lcov_d fcov_n fcov_d <<<"$covnums"
    if [ "$lcov_n" != "$lcov_d" ] || [ "$fcov_n" != "$fcov_d" ]; then
        fail "unit-test coverage below 100% — lines $lcov_n/$lcov_d, functions $fcov_n/$fcov_d (find gaps with: scripts/coverage.sh show <file>)"
    fi
    bold "Unit-test coverage: 100% lines ($lcov_n/$lcov_d) + functions ($fcov_n/$fcov_d)."
fi

# 2. Doc coverage: 100% Javadoc on the public gpu API (hard gate) ---------------------------------
if [ "${QA_GATE_SKIP_DOCS:-0}" = "1" ]; then
    bold "Skipping documentation-coverage stage (QA_GATE_SKIP_DOCS=1)."
else
    bold "Checking documentation coverage (100% Javadoc)…"
    bash scripts/doc_coverage.sh || fail "documentation coverage below 100% — document the entities listed above"
fi

# 2c. Module sidecars: the .sha512 that marks gpu.hpp a VERIFIED cheatah module, so `biome add
#     cheatah-gpu` resolves it on the extension path. Regenerate + fail if it drifted from gpu.hpp.
bold "Checking module sidecars (biome-installability) are in sync…"
bash scripts/sign-modules.sh >/dev/null || fail "sign-modules.sh"
if [ -n "$(git status --porcelain -- 'gpu/gpu.hpp.sha512')" ]; then
    fail "gpu/gpu.hpp.sha512 drifted from gpu/gpu.hpp — run scripts/sign-modules.sh, commit it, push again"
fi

# 3. Configure + build (debug) -------------------------------------------------------------------
bold "Configuring + building (debug)…"
cmake --preset debug         >/tmp/cheatah_gpu_cfg_debug.log   2>&1 || { tail -20 /tmp/cheatah_gpu_cfg_debug.log;   fail "configure (debug)"; }
cmake --build --preset debug >/tmp/cheatah_gpu_build_debug.log 2>&1 || { tail -30 /tmp/cheatah_gpu_build_debug.log; fail "debug build"; }

# 4. System tests: compile each systests/test_*.purr against gpu/ and run it ----------------------
[ -n "${CHEATAH_DIR:-}" ] && [ -d "$CHEATAH_DIR" ] || fail "cannot find the cheatah toolchain (set CHEATAH_DIR or place it at ../cheatah)"
find_tool() { local n="$1"; for c in release debug asan; do
    [ -x "$CHEATAH_DIR/build/$c/bin/$n" ] && { echo "$CHEATAH_DIR/build/$c/bin/$n"; return 0; }; done; return 1; }
PURRC="$(find_tool purrc || true)"; CHEATAH="$(find_tool cheatah || true)"
if [ -z "$PURRC" ] || [ -z "$CHEATAH" ]; then
    bold "building the cheatah toolchain (no prebuilt purrc/cheatah found)…"
    cmake -S "$CHEATAH_DIR" -B "$CHEATAH_DIR/build/release" -DCMAKE_BUILD_TYPE=Release >/dev/null \
      && cmake --build "$CHEATAH_DIR/build/release" --target purrc cheatah >/dev/null \
      || fail "failed to build the cheatah toolchain"
    PURRC="$(find_tool purrc)"; CHEATAH="$(find_tool cheatah)"
fi
bold "Running cheatah (.purr) system tests…  purrc=$PURRC"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
# The shared test helper is pure cheatah; emit it to an importable header so `import check` resolves.
"$PURRC" --emit-library --transparent "$REPO_ROOT/systests/check.purr" -o "$WORK/check.hpp" 2>"$WORK/check.err" \
  || { red "failed to build the test helper (systests/check.purr):"; sed 's/^/    /' "$WORK/check.err"; exit 1; }
shopt -s nullglob
tests=("$REPO_ROOT"/systests/test_*.purr)
[ ${#tests[@]} -gt 0 ] || fail "no systests/test_*.purr — refusing to pass a gate with nothing to check"
# `import gpu` pulls the Vulkan surface (gpu.hpp -> commands.hpp -> types.hpp), generated from the
# vendored registry. Compile against the NEWEST installed SDK headers (what cheatah-gpu provisions and
# what a biome user gets), not the box's possibly-stale /usr/include — same as the Vulkan gate.
SDK_INC="$(ls -d "$HOME"/Tools/vulkan-sdk/*/x86_64/include "$HOME"/VulkanSDK/*/x86_64/include 2>/dev/null | sort -V | tail -1)"
[ -n "$SDK_INC" ] && bold "Using Vulkan SDK headers: $SDK_INC"
sfails=0; sran=0
for t in "${tests[@]}"; do
    name="$(basename "$t" .purr)"; sran=$((sran + 1)); mod="$WORK/$name.so"
    bold "── $name ──"
    if ! CPATH="${SDK_INC:+$SDK_INC:}${CPATH:-}" "$PURRC" --import-root "$REPO_ROOT" --import-root "$WORK" "$t" -o "$mod" 2>"$WORK/$name.err"; then
        red "  COMPILE FAILED"; sed 's/^/    /' "$WORK/$name.err"; sfails=$((sfails + 1)); continue
    fi
    out="$("$CHEATAH" "$mod" 2>&1)"; echo "$out" | sed 's/^/  /'
    if echo "$out" | grep -q "FAIL" || ! echo "$out" | grep -q "RESULT: PASS"; then
        red "  TEST FAILED"; sfails=$((sfails + 1))
    fi
done
[ "$sfails" -eq 0 ] || fail "$sfails of $sran system test file(s) failed"
green "[qa-gate] system tests: $sran/$sran green."

# 4b. Biome-install sandbox: prove a standard cheatah install can `biome add cheatah-gpu` and use it
#     (extension path + sidecar; no git, no --import-root) — the first-real-extension contract.
bold "Sandboxing the 'biome add cheatah-gpu' user experience…"
CHEATAH_DIR="$CHEATAH_DIR" bash scripts/test-biome-install.sh || fail "biome-install sandbox"

# 5. Unit tests (hard gate) ----------------------------------------------------------------------
# Exclude the `qa_gate` ctest entry itself — it shells back into THIS script (infinite recursion).
bold "Running unit test suite…"
ctest --preset debug --output-on-failure --exclude-regex '^qa_gate$' || fail "unit tests"

# 6. Sanitizers: ASan + UBSan (hard gate) --------------------------------------------------------
if [ "${QA_GATE_SKIP_ASAN:-0}" = "1" ]; then
    bold "Skipping sanitizer stage (QA_GATE_SKIP_ASAN=1)."
else
    bold "Configuring + building (ASan + UBSan)…"
    cmake --preset asan         >/tmp/cheatah_gpu_cfg_asan.log   2>&1 || { tail -20 /tmp/cheatah_gpu_cfg_asan.log;   fail "configure (asan)"; }
    cmake --build --preset asan >/tmp/cheatah_gpu_build_asan.log 2>&1 || { tail -30 /tmp/cheatah_gpu_build_asan.log; fail "asan build"; }
    bold "Running unit test suite under ASan + UBSan…"
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" \
        ctest --preset asan --output-on-failure --exclude-regex '^qa_gate$' || fail "sanitizer (ASan/UBSan) tests"
fi

# 7. Valgrind memcheck (hard gate) ---------------------------------------------------------------
if [ "${QA_GATE_SKIP_VALGRIND:-0}" = "1" ]; then
    bold "Skipping Valgrind stage (QA_GATE_SKIP_VALGRIND=1)."
elif ! command -v valgrind >/dev/null 2>&1; then
    fail "valgrind not installed (install it, or set QA_GATE_SKIP_VALGRIND=1)"
else
    bold "Running unit tests under Valgrind memcheck…"
    bash security/run-valgrind.sh >/tmp/cheatah_gpu_valgrind.log 2>&1 || { tail -50 /tmp/cheatah_gpu_valgrind.log; fail "valgrind memcheck"; }
    tail -1 /tmp/cheatah_gpu_valgrind.log
fi

# 8. Static analysis: cppcheck (hard gate) -------------------------------------------------------
bold "Running cppcheck (performance + security)…"
bash scripts/cppcheck.sh || fail "cppcheck (performance/security findings)"

bold "QA gate PASSED — push may proceed."
exit 0
