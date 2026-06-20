// block_stubs.cpp — minimal Blocks-runtime symbols.
//
// metal-cpp, built with MTL_PRIVATE_IMPLEMENTATION, instantiates a few methods that use Clang blocks
// (e.g. device enumeration with an observer block). The software emulator never calls those paths, but
// the symbols must resolve at link time. Providing these stubs avoids a dependency on libBlocksRuntime
// off Apple. They are never executed in the emulated build.
#include <cstddef>

extern "C" {
void* _NSConcreteStackBlock[32] = {nullptr};
void* _NSConcreteGlobalBlock[32] = {nullptr};
void _Block_object_assign(void*, const void*, int) {}
void _Block_object_dispose(const void*, int) {}
void* _Block_copy(const void* b) { return const_cast<void*>(b); }
void _Block_release(const void*) {}
}
