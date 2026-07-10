// emulated_metal.cpp — a software Metal device for platforms without Apple hardware.
//
// cheatah-gpu's Metal backend is metal-cpp talking to the real Metal framework on Apple. Off Apple
// (Linux/CI), metal-cpp has no runtime, so this file PROVIDES one: a tiny Objective-C message send
// plus software implementations of the Metal compute objects (device, queue, command buffer, compute
// encoder, buffer, library, function, pipeline). It lets the SAME metal-cpp code compile AND run a
// compute kernel on the CPU — the Metal analogue of Mesa llvmpipe — so the bindings are testable here.
//
// Built-in kernels stand in for compiled MSL (no Metal shader compiler off Apple): the host registers
// a C++ function by name via cheatah_metal_emu_register(), and a dispatch runs it over the bound
// buffers. Memory is REFERENCE-COUNTED (metal-cpp's retain/release free objects for real), so a
// correctly-written program leaks nothing under Valgrind/ASan. Single-threaded by design — cheatah-gpu
// never threads internally — so there is nothing for Helgrind to flag.
//
// Leak alerting (NOT RAII enforcement): build with -DCHEATAH_GPU_METAL_LEAKCHECK=1 to track live
// objects and report any survivors via cheatah_metal_emu_live_objects(); compiled out by default so
// production pays nothing.

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

enum Tag {
    T_CLASS, T_STR, T_DEV, T_QUEUE, T_LIB, T_FUNC, T_PIPE, T_BUF, T_CMDBUF, T_ENC, T_POOL, T_OBJ,
    // The render/texture path: enough to create a 2D texture, clear it through a render pass, and
    // read the pixels back — the graphics analogue of the compute objects above.
    T_TEXDESC, T_TEX, T_RPDESC, T_RPCAA, T_RPCA, T_RENC,
};

struct Base {
    void* isa = nullptr;
    int tag = T_OBJ;
    int rc = 1;
};
struct ClassObj : Base { std::string name; };
struct Str : Base { std::string s; };
struct Func : Base { std::string name; };
struct Pipe : Base { std::string fn; };
struct Buf : Base { std::vector<unsigned char> data; };
struct CmdBuf : Base { std::vector<std::function<void()>> work; };
struct Enc : Base { CmdBuf* cb = nullptr; std::string fn; std::map<unsigned long, std::pair<Buf*, unsigned long>> bufs; };

// --- the render/texture objects ------------------------------------------------------------------
// Only RGBA8Unorm is emulated (bytes in memory are R,G,B,A) — the one format cheatah-gpu's consumers
// use for an offscreen target, and byte-for-byte what Vulkan's VK_FORMAT_R8G8B8A8_UNORM gives.
constexpr unsigned long kBytesPerPixel = 4;

struct TexDesc : Base {
    unsigned long width = 0, height = 0;
    unsigned long pixel_format = 0, usage = 0, storage_mode = 0, texture_type = 0;
};
struct Tex : Base {
    unsigned long width = 0, height = 0, pixel_format = 0;
    std::vector<unsigned char> data;  ///< width * height * 4, row-major, tightly packed
};
/// One colour attachment of a render pass: which texture, and what to do with it on load/store.
struct RPCA : Base {
    Tex* texture = nullptr;
    unsigned long load_action = 0;   ///< MTL::LoadActionDontCare(0) / Load(1) / Clear(2)
    unsigned long store_action = 0;
    double clear_color[4] = {0.0, 0.0, 0.0, 0.0};
};
/// The attachment array. Owns its attachments (they are unowned views to metal-cpp).
struct RPCAA : Base {
    std::map<unsigned long, RPCA*> attachments;
    ~RPCAA() { for (auto& kv : attachments) { delete kv.second; } }
};
struct RPDesc : Base {
    RPCAA* colors = nullptr;  ///< created lazily by `colorAttachments`; owned
    ~RPDesc() { delete colors; }
};
/// A render encoder. Clear-only for now: the work is queued at endEncoding, run at commit.
struct REnc : Base { CmdBuf* cb = nullptr; RPDesc* pass = nullptr; };

/// Metal's float->unorm8 conversion: round to nearest, matching what a real GPU stores (and what
/// Vulkan's UNORM clear produces), so a clear of 0.25/0.5/0.75/1.0 reads back as 64/128/191/255.
unsigned char to_unorm8(double c) {
    if (c <= 0.0) { return 0; }
    if (c >= 1.0) { return 255; }
    return static_cast<unsigned char>(c * 255.0 + 0.5);
}

// Function-local statics dodge the static-init-order fiasco: metal-cpp registers its classes/selectors
// during static init, which calls back into sel_registerName / objc_lookUpClass below.
std::set<std::string>& sel_pool() { static std::set<std::string> s; return s; }
std::map<std::string, ClassObj*>& classes() { static std::map<std::string, ClassObj*> m; return m; }
std::vector<std::vector<Base*>>& autorelease_stack() { static std::vector<std::vector<Base*>> v; return v; }

using Kernel = std::function<void(void**, unsigned, unsigned long)>;
std::map<std::string, Kernel>& kernels() { static std::map<std::string, Kernel> k; return k; }

#if defined(CHEATAH_GPU_METAL_LEAKCHECK) && CHEATAH_GPU_METAL_LEAKCHECK
std::set<Base*>& live() { static std::set<Base*> s; return s; }
void track(Base* b) { live().insert(b); }
void untrack(Base* b) { live().erase(b); }
#else
inline void track(Base*) {}
inline void untrack(Base*) {}
#endif

// Destroy by tag so the right (non-trivial) destructor runs.
void destroy(Base* b) {
    untrack(b);
    switch (b->tag) {
        case T_CLASS:  delete static_cast<ClassObj*>(b); break;
        case T_STR:    delete static_cast<Str*>(b); break;
        case T_FUNC:   delete static_cast<Func*>(b); break;
        case T_PIPE:   delete static_cast<Pipe*>(b); break;
        case T_BUF:    delete static_cast<Buf*>(b); break;
        case T_CMDBUF:
        case T_QUEUE:  delete static_cast<CmdBuf*>(b); break;
        case T_ENC:    delete static_cast<Enc*>(b); break;
        case T_TEXDESC: delete static_cast<TexDesc*>(b); break;
        case T_TEX:    delete static_cast<Tex*>(b); break;
        case T_RPDESC: delete static_cast<RPDesc*>(b); break;  // frees its attachment array
        case T_RENC:   delete static_cast<REnc*>(b); break;
        default:       delete b; break;
    }
}

template <typename T> T* make(int tag) { T* o = new T(); o->tag = tag; track(o); return o; }
void retain(Base* b) { if (b) ++b->rc; }
void release(Base* b) { if (b && --b->rc <= 0) destroy(b); }
void autorelease(Base* b) { if (b && !autorelease_stack().empty()) autorelease_stack().back().push_back(b); }

}  // namespace

// The host-facing control surface for the emulator is a normal cheatah module (no C linkage): register
// a stand-in compute kernel by name, and query live objects to assert leak-freedom. See emulated.hpp.
namespace cheatah::gpu::metal::emulated {

void register_kernel(const char* name, void (*fn)(void**, unsigned, unsigned long)) { kernels()[name] = fn; }

unsigned long live_objects() {
#if defined(CHEATAH_GPU_METAL_LEAKCHECK) && CHEATAH_GPU_METAL_LEAKCHECK
    return static_cast<unsigned long>(live().size());
#else
    return 0;
#endif
}

}  // namespace cheatah::gpu::metal::emulated

// The Objective-C runtime + Metal entry points metal-cpp links against MUST keep C linkage and these
// exact names — they are the platform ABI metal-cpp expects, here implemented in software.
extern "C" {

SEL sel_registerName(const char* n) { return reinterpret_cast<SEL>(const_cast<char*>(sel_pool().insert(n).first->c_str())); }
const char* sel_getName(SEL s) { return reinterpret_cast<const char*>(s); }

Class objc_lookUpClass(const char* n) {
    auto it = classes().find(n);
    if (it != classes().end()) return reinterpret_cast<Class>(it->second);
    ClassObj* c = make<ClassObj>(T_CLASS);
    untrack(c);  // classes are permanent runtime singletons, not leakable objects
    c->name = n;
    classes()[n] = c;
    return reinterpret_cast<Class>(c);
}
Class objc_getClass(const char* n) { return objc_lookUpClass(n); }
Protocol* objc_getProtocol(const char*) { return nullptr; }
Class object_getClass(id o) { return o ? reinterpret_cast<Class>(reinterpret_cast<Base*>(o)->isa) : nullptr; }
const char* class_getName(Class) { return "EmulatedMetal"; }
id objc_retain(id o) { retain(reinterpret_cast<Base*>(o)); return o; }
void objc_release(id o) { release(reinterpret_cast<Base*>(o)); }
id objc_autorelease(id o) { autorelease(reinterpret_cast<Base*>(o)); return o; }
void objc_msgSend_stret(void* ret, id, SEL, ...) { if (ret) std::memset(ret, 0, sizeof(void*)); }
double objc_msgSend_fpret(id, SEL, ...) { return 0.0; }

// The message send. metal-cpp casts &objc_msgSend to the call-site's exact signature, so the variadic
// here is read back per the same ABI for the selectors we implement.
id objc_msgSend(id self, SEL op, ...) {
    if (!self) return nullptr;
    Base* b = reinterpret_cast<Base*>(self);
    const std::string s = sel_getName(op);
    va_list ap;
    va_start(ap, op);
    id ret = nullptr;

    if (b->tag == T_CLASS) {
        const std::string& cls = static_cast<ClassObj*>(b)->name;
        if (s == "alloc") {
            // `alloc` is the one place the class name decides the object, so a later `init` (which
            // just returns self) yields a descriptor of the right kind.
            if (cls == "NSAutoreleasePool") { ret = reinterpret_cast<id>(make<Base>(T_POOL)); }
            else if (cls == "MTLTextureDescriptor") { ret = reinterpret_cast<id>(make<TexDesc>(T_TEXDESC)); }
            else if (cls == "MTLRenderPassDescriptor") { ret = reinterpret_cast<id>(make<RPDesc>(T_RPDESC)); }
            else { ret = reinterpret_cast<id>(make<Base>(T_OBJ)); }
        } else if (s.rfind("stringWithCString", 0) == 0 || s.rfind("stringWithUTF8", 0) == 0) {
            const char* c = va_arg(ap, const char*);
            Str* o = make<Str>(T_STR); o->s = c ? c : ""; autorelease(o); ret = reinterpret_cast<id>(o);
        } else if (s == "string") {
            Str* o = make<Str>(T_STR); autorelease(o); ret = reinterpret_cast<id>(o);
        }
    } else {
        // Reference-count + autorelease ops, applied exactly once. "release"/"drain" on a pool also
        // releases everything autoreleased into it.
        if (s == "retain") { retain(b); va_end(ap); return self; }
        if (s == "autorelease") { autorelease(b); va_end(ap); return self; }
        if (s == "release" || (s == "drain" && b->tag == T_POOL)) {
            if (b->tag == T_POOL && !autorelease_stack().empty()) {
                auto pooled = autorelease_stack().back();
                autorelease_stack().pop_back();
                for (Base* p : pooled) release(p);
            }
            release(b);
            va_end(ap);
            return nullptr;
        }
        // `init` returns self for every object; a pool additionally opens an autorelease scope.
        if (s == "init") {
            if (b->tag == T_POOL) { autorelease_stack().emplace_back(); }
            va_end(ap);
            return self;
        }
        switch (b->tag) {
            case T_DEV:
                if (s == "newCommandQueue" || s == "newCommandQueueWithMaxCommandBufferCount:")
                    ret = reinterpret_cast<id>(make<CmdBuf>(T_QUEUE));
                else if (s.rfind("newLibraryWithSource", 0) == 0)
                    ret = reinterpret_cast<id>(make<Base>(T_LIB));
                else if (s.rfind("newComputePipelineStateWithFunction", 0) == 0) {
                    Func* f = reinterpret_cast<Func*>(va_arg(ap, id));
                    Pipe* p = make<Pipe>(T_PIPE); p->fn = f ? f->name : ""; ret = reinterpret_cast<id>(p);
                } else if (s == "newBufferWithLength:options:") {
                    unsigned long len = va_arg(ap, unsigned long);
                    Buf* bf = make<Buf>(T_BUF); bf->data.assign(len, 0); ret = reinterpret_cast<id>(bf);
                } else if (s == "newBufferWithBytes:length:options:") {
                    void* p = va_arg(ap, void*); unsigned long len = va_arg(ap, unsigned long);
                    Buf* bf = make<Buf>(T_BUF);
                    bf->data.assign(static_cast<unsigned char*>(p), static_cast<unsigned char*>(p) + len);
                    ret = reinterpret_cast<id>(bf);
                } else if (s == "newTextureWithDescriptor:") {
                    TexDesc* d = reinterpret_cast<TexDesc*>(va_arg(ap, id));
                    Tex* t = make<Tex>(T_TEX);
                    if (d != nullptr) {
                        t->width = d->width; t->height = d->height; t->pixel_format = d->pixel_format;
                    }
                    t->data.assign(t->width * t->height * kBytesPerPixel, 0);
                    ret = reinterpret_cast<id>(t);
                }
                break;
            case T_LIB:
                if (s == "newFunctionWithName:") {
                    Str* nm = reinterpret_cast<Str*>(va_arg(ap, id));
                    Func* f = make<Func>(T_FUNC); f->name = nm ? nm->s : ""; ret = reinterpret_cast<id>(f);
                }
                break;
            case T_QUEUE:
                if (s == "commandBuffer" || s == "commandBufferWithUnretainedReferences") {
                    CmdBuf* c = make<CmdBuf>(T_CMDBUF); autorelease(c); ret = reinterpret_cast<id>(c);
                }
                break;
            case T_CMDBUF: {
                CmdBuf* c = static_cast<CmdBuf*>(b);
                if (s == "computeCommandEncoder") {
                    Enc* e = make<Enc>(T_ENC); e->cb = c; autorelease(e); ret = reinterpret_cast<id>(e);
                } else if (s == "renderCommandEncoderWithDescriptor:") {
                    REnc* e = make<REnc>(T_RENC);
                    e->cb = c;
                    e->pass = reinterpret_cast<RPDesc*>(va_arg(ap, id));
                    autorelease(e);
                    ret = reinterpret_cast<id>(e);
                } else if (s == "commit") {
                    for (auto& w : c->work) w();
                }  // waitUntilCompleted: synchronous already
                break;
            }
            case T_ENC: {
                Enc* e = static_cast<Enc*>(b);
                if (s == "setComputePipelineState:") {
                    Pipe* p = reinterpret_cast<Pipe*>(va_arg(ap, id)); e->fn = p ? p->fn : "";
                } else if (s == "setBuffer:offset:atIndex:") {
                    Buf* bf = reinterpret_cast<Buf*>(va_arg(ap, id));
                    unsigned long off = va_arg(ap, unsigned long);
                    unsigned long idx = va_arg(ap, unsigned long);
                    e->bufs[idx] = {bf, off};
                } else if (s.rfind("dispatchThreads:", 0) == 0 || s.rfind("dispatchThreadgroups:", 0) == 0) {
                    MTL::Size grid = va_arg(ap, MTL::Size); va_arg(ap, MTL::Size);
                    unsigned long grid_width = grid.width;
                    std::string fn = e->fn; auto bufs = e->bufs;
                    e->cb->work.push_back([fn, bufs, grid_width]() mutable {
                        auto it = kernels().find(fn);
                        if (it == kernels().end()) return;
                        unsigned long maxi = 0; for (auto& kv : bufs) if (kv.first > maxi) maxi = kv.first;
                        std::vector<void*> ptrs(maxi + 1, nullptr);
                        for (auto& kv : bufs) ptrs[kv.first] = kv.second.first->data.data() + kv.second.second;
                        it->second(ptrs.data(), static_cast<unsigned>(ptrs.size()), grid_width);
                    });
                }
                break;
            }
            case T_BUF: {
                Buf* bf = static_cast<Buf*>(b);
                if (s == "contents") ret = reinterpret_cast<id>(bf->data.data());
                else if (s == "length") ret = reinterpret_cast<id>(static_cast<uintptr_t>(bf->data.size()));
                break;
            }
            case T_TEXDESC: {
                TexDesc* d = static_cast<TexDesc*>(b);
                if (s == "setWidth:") d->width = va_arg(ap, unsigned long);
                else if (s == "setHeight:") d->height = va_arg(ap, unsigned long);
                else if (s == "setPixelFormat:") d->pixel_format = va_arg(ap, unsigned long);
                else if (s == "setUsage:") d->usage = va_arg(ap, unsigned long);
                else if (s == "setStorageMode:") d->storage_mode = va_arg(ap, unsigned long);
                else if (s == "setTextureType:") d->texture_type = va_arg(ap, unsigned long);
                else if (s == "width") ret = reinterpret_cast<id>(static_cast<uintptr_t>(d->width));
                else if (s == "height") ret = reinterpret_cast<id>(static_cast<uintptr_t>(d->height));
                break;
            }
            case T_TEX: {
                Tex* t = static_cast<Tex*>(b);
                if (s == "width") ret = reinterpret_cast<id>(static_cast<uintptr_t>(t->width));
                else if (s == "height") ret = reinterpret_cast<id>(static_cast<uintptr_t>(t->height));
                else if (s == "getBytes:bytesPerRow:fromRegion:mipmapLevel:") {
                    // (void* bytes, NS::UInteger bytesPerRow, MTL::Region region, NS::UInteger level)
                    unsigned char* out = static_cast<unsigned char*>(va_arg(ap, void*));
                    unsigned long dst_stride = va_arg(ap, unsigned long);
                    MTL::Region r = va_arg(ap, MTL::Region);
                    va_arg(ap, unsigned long);  // mipmapLevel — only level 0 exists here
                    const unsigned long src_stride = t->width * kBytesPerPixel;
                    if (out != nullptr && r.origin.x + r.size.width <= t->width &&
                        r.origin.y + r.size.height <= t->height) {
                        for (unsigned long row = 0; row < r.size.height; ++row) {
                            const unsigned char* src =
                                t->data.data() + (r.origin.y + row) * src_stride + r.origin.x * kBytesPerPixel;
                            std::memcpy(out + row * dst_stride, src, r.size.width * kBytesPerPixel);
                        }
                    }
                } else if (s == "replaceRegion:mipmapLevel:withBytes:bytesPerRow:") {
                    // (MTL::Region region, NS::UInteger level, const void* bytes, NS::UInteger bpr)
                    MTL::Region r = va_arg(ap, MTL::Region);
                    va_arg(ap, unsigned long);  // mipmapLevel
                    const unsigned char* in = static_cast<const unsigned char*>(va_arg(ap, void*));
                    unsigned long src_stride = va_arg(ap, unsigned long);
                    const unsigned long dst_stride = t->width * kBytesPerPixel;
                    if (in != nullptr && r.origin.x + r.size.width <= t->width &&
                        r.origin.y + r.size.height <= t->height) {
                        for (unsigned long row = 0; row < r.size.height; ++row) {
                            unsigned char* dst =
                                t->data.data() + (r.origin.y + row) * dst_stride + r.origin.x * kBytesPerPixel;
                            std::memcpy(dst, in + row * src_stride, r.size.width * kBytesPerPixel);
                        }
                    }
                }
                break;
            }
            case T_RPDESC: {
                RPDesc* d = static_cast<RPDesc*>(b);
                if (s == "colorAttachments") {
                    if (d->colors == nullptr) {
                        d->colors = make<RPCAA>(T_RPCAA);
                        untrack(d->colors);  // an unowned view to metal-cpp; the descriptor frees it
                    }
                    ret = reinterpret_cast<id>(d->colors);
                }
                break;
            }
            case T_RPCAA: {
                RPCAA* arr = static_cast<RPCAA*>(b);
                if (s == "objectAtIndexedSubscript:") {
                    unsigned long i = va_arg(ap, unsigned long);
                    RPCA*& slot = arr->attachments[i];
                    if (slot == nullptr) {
                        slot = make<RPCA>(T_RPCA);
                        untrack(slot);  // unowned view; the array frees it
                    }
                    ret = reinterpret_cast<id>(slot);
                }
                break;
            }
            case T_RPCA: {
                RPCA* att = static_cast<RPCA*>(b);
                if (s == "setTexture:") att->texture = reinterpret_cast<Tex*>(va_arg(ap, id));
                else if (s == "setLoadAction:") att->load_action = va_arg(ap, unsigned long);
                else if (s == "setStoreAction:") att->store_action = va_arg(ap, unsigned long);
                else if (s == "setClearColor:") {
                    MTL::ClearColor c = va_arg(ap, MTL::ClearColor);
                    att->clear_color[0] = c.red; att->clear_color[1] = c.green;
                    att->clear_color[2] = c.blue; att->clear_color[3] = c.alpha;
                }
                break;
            }
            case T_RENC: {
                REnc* e = static_cast<REnc*>(b);
                if (s == "endEncoding" && e->pass != nullptr && e->pass->colors != nullptr) {
                    // A clear-only render pass: at commit, every attachment whose loadAction is Clear
                    // fills its texture with the clear colour. (Draw calls arrive with the mesh work.)
                    for (auto& kv : e->pass->colors->attachments) {
                        RPCA* att = kv.second;
                        if (att->texture == nullptr || att->load_action != MTL::LoadActionClear) { continue; }
                        Tex* t = att->texture;
                        const std::array<unsigned char, kBytesPerPixel> px = {
                            to_unorm8(att->clear_color[0]), to_unorm8(att->clear_color[1]),
                            to_unorm8(att->clear_color[2]), to_unorm8(att->clear_color[3])};
                        e->cb->work.push_back([t, px]() {
                            for (std::size_t i = 0; i + kBytesPerPixel <= t->data.size(); i += kBytesPerPixel) {
                                std::memcpy(t->data.data() + i, px.data(), kBytesPerPixel);
                            }
                        });
                    }
                }
                break;
            }
            case T_STR: {
                Str* st = static_cast<Str*>(b);
                if (s.rfind("cStringUsingEncoding", 0) == 0 || s == "UTF8String") ret = reinterpret_cast<id>(const_cast<char*>(st->s.c_str()));
                else if (s == "length") ret = reinterpret_cast<id>(static_cast<uintptr_t>(st->s.size()));
                break;
            }
            default: break;
        }
    }
    va_end(ap);
    return ret;
}

// The one Metal entry point that is a plain C function, not a message: device creation.
MTL::Device* MTLCreateSystemDefaultDevice() { return reinterpret_cast<MTL::Device*>(make<Base>(T_DEV)); }

}  // extern "C"
