# C++ unit tests for the gpu package's hand-authored headers.
#
# The single binary `cheatah_gpu_tests` is what scripts/coverage.sh instruments (clang source-based
# coverage) and what security/run-valgrind.sh memchecks. Linking the cheatah::gpu INTERFACE target
# puts the repo root on the include path so `#include "gpu/dispatch/dispatch.hpp"` resolves.
include(GoogleTest)

add_executable(cheatah_gpu_tests
    dispatch_test.cpp
    backend_test.cpp
)
target_link_libraries(cheatah_gpu_tests PRIVATE
    cheatah::gpu
    GTest::gtest_main
)

# Optional ASan + UBSan (the `asan` preset turns this on). Valgrind runs the plain debug build.
if(CHEATAH_GPU_SANITIZE)
    target_compile_options(cheatah_gpu_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cheatah_gpu_tests PRIVATE
        -fsanitize=address,undefined)
endif()

gtest_discover_tests(cheatah_gpu_tests)
