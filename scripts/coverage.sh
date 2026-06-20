#!/usr/bin/env bash
# Measure cheatah-gpu test coverage with clang source-based coverage (the in-process unit tests,
# cheatah_gpu_tests, which exercise the hand-authored gpu/ headers).
#
#   scripts/coverage.sh               # per-file summary report for gpu/
#   scripts/coverage.sh show <file>   # uncovered lines of one file, e.g. gpu/dispatch/dispatch.hpp
#   scripts/coverage.sh funcs <file>  # per-function coverage of one file
#   scripts/coverage.sh update-readme # rewrite the coverage table in README.md
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

B=build/cov
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCHEATAH_GPU_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" >/tmp/cheatah_gpu_cov_cfg.log 2>&1 \
  || { tail -15 /tmp/cheatah_gpu_cov_cfg.log; exit 1; }
cmake --build "$B" --target cheatah_gpu_tests >/tmp/cheatah_gpu_cov_build.log 2>&1 \
  || { tail -25 /tmp/cheatah_gpu_cov_build.log; exit 1; }

( cd "$B"
  LLVM_PROFILE_FILE=t1.profraw ./bin/cheatah_gpu_tests >/dev/null 2>&1
  llvm-profdata merge -sparse t1.profraw -o merged.profdata )

OBJS=(./"$B"/bin/cheatah_gpu_tests)
PROF="-instr-profile=$B/merged.profdata"
# The hand-written, host-testable surface (git's '*' spans '/'). The generated Vulkan forwarders
# (gpu/vulkan/*) need a GPU + the 3-device matrix, so they are covered by the separate Vulkan gate,
# not this host gate — excluded here and graduated in as they get tests.
SRCS=$(git ls-files 'gpu/*.hpp' | grep -vE '/tests/|^gpu/vulkan/')

case "${1:-report}" in
    show)  llvm-cov show   "${OBJS[@]}" $PROF "${2:?usage: coverage.sh show <file>}" 2>/dev/null \
             | grep -nE '\|[[:space:]]*0\|' || echo "all lines covered in ${2}" ;;
    funcs) llvm-cov report "${OBJS[@]}" $PROF -show-functions "${2:?usage: coverage.sh funcs <file>}" 2>/dev/null ;;
    update-readme)
        # All metrics come from llvm-cov's per-file report TOTAL (no Python — covered = total - missed).
        # Columns: TOTAL Regions MissedReg RCov Funcs MissedFun FExec Lines MissedLines LCov Branches MissedBr BCov
        read -r reg mreg rcov fun mfun fexec lines mlin lcov br mbr bcov < <(
            llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null \
              | awk '$1=="TOTAL"{print $2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13}')
        : "${lines:=0}" "${mlin:=0}" "${fun:=0}" "${mfun:=0}"
        lcn=$((lines - mlin)); fcn=$((fun - mfun))
        tbl="$(mktemp)"
        {
            echo "<!-- coverage:start -->"
            echo "| Metric | gpu package |"
            echo "|--------|-------------|"
            echo "| **Lines** | $lcov ($lcn/$lines) |"
            echo "| **Functions** | $fexec ($fcn/$fun) |"
            echo "| Regions | $rcov |"
            echo "| Branches | $bcov |"
            echo "<!-- coverage:end -->"
        } > "$tbl"
        # Splice the table between the markers in README.md (awk, no Python).
        awk -v tf="$tbl" '
            /<!-- coverage:start -->/ { while ((getline l < tf) > 0) print l; close(tf); skip=1; next }
            /<!-- coverage:end -->/   { skip=0; next }
            !skip { print }
        ' README.md > README.md.new && mv README.md.new README.md
        rm -f "$tbl"
        echo "README coverage table: lines $lcov ($lcn/$lines), functions $fexec ($fcn/$fun)"
        ;;
    *)     llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null ;;
esac
