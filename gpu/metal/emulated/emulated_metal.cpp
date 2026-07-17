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

#include "emulated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    // The render/texture path: create a 2D texture, clear it through a render pass, read the pixels
    // back — and DRAW: the render encoder rasterizes depth-tested, textured, indexed triangles (the
    // graphics analogue of the registered compute kernels; see raster_draw below).
    T_TEXDESC, T_TEX, T_RPDESC, T_RPCAA, T_RPCA, T_RPDA, T_RENC,
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
/// The depth attachment of a render pass — texture, load/store, clear depth. Enough for LESS + write.
struct RPDA : Base {
    Tex* texture = nullptr;
    unsigned long load_action = 0;
    unsigned long store_action = 0;
    double clear_depth = 1.0;
};
struct RPDesc : Base {
    RPCAA* colors = nullptr;  ///< created lazily by `colorAttachments`; owned
    RPDA* depth = nullptr;    ///< created lazily by `depthAttachment`; owned
    ~RPDesc() { delete colors; delete depth; }
};
/// A render encoder: the pass's load actions queue at CREATION (so a clear orders before the draws),
/// draw state accumulates via the set* selectors, and each drawIndexedPrimitives queues a raster.
struct REnc : Base {
    CmdBuf* cb = nullptr;
    RPDesc* pass = nullptr;
    unsigned long fill_mode = 0;  ///< MTL::TriangleFillModeFill; Lines draws edges
    std::map<unsigned long, std::pair<Buf*, unsigned long>> vbufs;        ///< setVertexBuffer (buf, offset)
    std::map<unsigned long, std::vector<unsigned char>> vbytes;           ///< setVertexBytes copies
    std::map<unsigned long, std::vector<unsigned char>> fbytes;           ///< setFragmentBytes copies
    std::map<unsigned long, Tex*> ftex;                                   ///< setFragmentTexture
};

/// Metal's float->unorm8 conversion: round to nearest, matching what a real GPU stores (and what
/// Vulkan's UNORM clear produces), so a clear of 0.25/0.5/0.75/1.0 reads back as 64/128/191/255.
unsigned char to_unorm8(double c) {
    if (c <= 0.0) { return 0; }
    if (c >= 1.0) { return 255; }
    return static_cast<unsigned char>(c * 255.0 + 0.5);
}

// --- the software render pipeline ------------------------------------------------------------------
// The emulated draw: a depth-tested, texture-sampling, indexed-triangle rasterizer implementing the
// STANDARD INTERLEAVED-MESH CONTRACT a slang mesh shader compiles to on Metal: stage_in vertices at
// buffer(1) — position float3 @0, normal float3 @12, uv float2 @24, stride 32 — an 80-byte constant
// at buffer(0) (column-major MVP then the light direction at byte 64), the base color at texture(0).
// Shading is the contract's textured Lambert:
// 0.25 + 0.75 * max(dot(n̂, L̂), 0) modulating the sampled texel. It is the C++ stand-in for compiled
// MSL, exactly as the registered compute kernels stand in for compute MSL (no shader compiler exists
// off Apple). Metal conventions throughout: NDC +y up onto a top-left-origin framebuffer, depth in
// [0,1] tested LESS with write (the contract's depth state), no culling.

/// One queued draw — the encoder state snapshot drawIndexedPrimitives captures for commit time.
struct DrawState {
    Tex* color = nullptr;
    Tex* depth = nullptr;                                   ///< null = no depth test
    unsigned long fill_mode = 0;                            ///< MTL::TriangleFillModeFill / Lines
    std::vector<unsigned char> push;                        ///< buffer(0): mvp[16] + light[3]
    Buf* vbuf = nullptr; unsigned long voff = 0;            ///< buffer(1): interleaved vertices
    Buf* ibuf = nullptr; unsigned long ioff = 0;
    unsigned long index_count = 0;
    unsigned long index_type = 0;                           ///< MTL::IndexTypeUInt16 / UInt32
    Tex* tex = nullptr;                                     ///< texture(0): the base color
};

void raster_draw(const DrawState& d) {
    if (d.color == nullptr || d.vbuf == nullptr || d.ibuf == nullptr || d.push.size() < 80 ||
        d.voff >= d.vbuf->data.size() || d.ioff >= d.ibuf->data.size()) {
        return;
    }
    const long w = static_cast<long>(d.color->width);
    const long h = static_cast<long>(d.color->height);
    if (w <= 0 || h <= 0 || d.color->data.size() < static_cast<std::size_t>(w) * h * kBytesPerPixel) {
        return;
    }
    float mvp[16];
    float light[3];
    std::memcpy(mvp, d.push.data(), sizeof(mvp));
    std::memcpy(light, d.push.data() + 64, sizeof(light));
    const float llen = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    if (llen > 0.0F) { light[0] /= llen; light[1] /= llen; light[2] /= llen; }  // uniform: normalize once

    float* depth = nullptr;
    if (d.depth != nullptr &&
        d.depth->data.size() >= static_cast<std::size_t>(w) * h * sizeof(float)) {
        depth = reinterpret_cast<float*>(d.depth->data.data());
    }
    const unsigned char* vb = d.vbuf->data.data() + d.voff;
    const std::size_t vb_size = d.vbuf->data.size() - d.voff;
    const unsigned char* ib = d.ibuf->data.data() + d.ioff;
    const std::size_t ib_size = d.ibuf->data.size() - d.ioff;
    const bool u16 = d.index_type == static_cast<unsigned long>(MTL::IndexTypeUInt16);

    // A projected vertex: screen position + depth + 1/w for perspective correction + the attributes.
    struct V { float sx, sy, sz, iw, nx, ny, nz, u, v; };
    auto fetch = [&](unsigned long index, V& out) -> bool {
        const std::size_t base = static_cast<std::size_t>(index) * 32;
        if (base + 32 > vb_size) { return false; }
        float in[8];
        std::memcpy(in, vb + base, sizeof(in));
        float clip[4];
        for (int r = 0; r < 4; ++r) {  // clip = MVP (column-major) * (pos, 1)
            clip[r] = mvp[0 * 4 + r] * in[0] + mvp[1 * 4 + r] * in[1] + mvp[2 * 4 + r] * in[2] +
                      mvp[3 * 4 + r];
        }
        if (clip[3] <= 1e-6F) { return false; }  // behind the eye — this stand-in does not near-clip
        const float iw = 1.0F / clip[3];
        out.sx = (clip[0] * iw * 0.5F + 0.5F) * static_cast<float>(w);
        out.sy = (1.0F - (clip[1] * iw * 0.5F + 0.5F)) * static_cast<float>(h);  // +y up -> row 0 top
        out.sz = clip[2] * iw;
        out.iw = iw;
        out.nx = in[3]; out.ny = in[4]; out.nz = in[5];
        out.u = in[6]; out.v = in[7];
        return true;
    };
    auto sample = [&](float u, float v, float px[3]) {
        if (d.tex == nullptr || d.tex->width == 0 || d.tex->height == 0) {
            px[0] = px[1] = px[2] = 1.0F;  // no texture bound: white, so lighting still shows
            return;
        }
        long tx = static_cast<long>(u * static_cast<float>(d.tex->width));
        long ty = static_cast<long>(v * static_cast<float>(d.tex->height));
        tx = std::min(std::max(tx, 0L), static_cast<long>(d.tex->width) - 1);   // clamp-to-edge
        ty = std::min(std::max(ty, 0L), static_cast<long>(d.tex->height) - 1);
        const unsigned char* p =
            d.tex->data.data() + (static_cast<std::size_t>(ty) * d.tex->width + tx) * kBytesPerPixel;
        px[0] = p[0] / 255.0F; px[1] = p[1] / 255.0F; px[2] = p[2] / 255.0F;
    };
    // Depth-test, shade (textured Lambert), and write one pixel.
    auto shade_write = [&](long x, long y, float z, float nx, float ny, float nz, float u, float v) {
        if (x < 0 || y < 0 || x >= w || y >= h || z < 0.0F || z > 1.0F) { return; }
        const std::size_t di = static_cast<std::size_t>(y) * w + x;
        if (depth != nullptr) {
            if (!(z < depth[di])) { return; }  // CompareFunctionLess
            depth[di] = z;                     // depth write enabled
        }
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        float ndl = 0.0F;
        if (nlen > 0.0F) {
            ndl = std::max((nx * light[0] + ny * light[1] + nz * light[2]) / nlen, 0.0F);
        }
        const float lit = 0.25F + 0.75F * ndl;
        float px[3];
        sample(u, v, px);
        unsigned char* out = d.color->data.data() + di * kBytesPerPixel;
        out[0] = to_unorm8(px[0] * lit);
        out[1] = to_unorm8(px[1] * lit);
        out[2] = to_unorm8(px[2] * lit);
        out[3] = 255;
    };
    // The signed doubled area of (a, b, c) — the edge function driving barycentrics.
    auto edge = [](double ax, double ay, double bx, double by, double cx, double cy) {
        return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    };

    const unsigned long tris = d.index_count / 3;
    for (unsigned long t = 0; t < tris; ++t) {
        unsigned long idx[3];
        bool idx_ok = true;
        for (int j = 0; j < 3; ++j) {
            const std::size_t k = t * 3 + j;
            if (u16) {
                if ((k + 1) * sizeof(std::uint16_t) > ib_size) { idx_ok = false; break; }
                idx[j] = reinterpret_cast<const std::uint16_t*>(ib)[k];
            } else {
                if ((k + 1) * sizeof(std::uint32_t) > ib_size) { idx_ok = false; break; }
                idx[j] = reinterpret_cast<const std::uint32_t*>(ib)[k];
            }
        }
        V v0, v1, v2;
        if (!idx_ok || !fetch(idx[0], v0) || !fetch(idx[1], v1) || !fetch(idx[2], v2)) { continue; }

        if (d.fill_mode == static_cast<unsigned long>(MTL::TriangleFillModeLines)) {
            // Wireframe: DDA the three edges, depth-tested, attributes lerped along each edge.
            const V* e[3][2] = {{&v0, &v1}, {&v1, &v2}, {&v2, &v0}};
            for (auto& ed : e) {
                const V& a = *ed[0];
                const V& b = *ed[1];
                const float dx = b.sx - a.sx, dy = b.sy - a.sy;
                const int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy))) + 1;
                for (int s = 0; s <= steps; ++s) {
                    const float f = static_cast<float>(s) / static_cast<float>(steps);
                    shade_write(static_cast<long>(a.sx + dx * f), static_cast<long>(a.sy + dy * f),
                                a.sz + (b.sz - a.sz) * f, a.nx + (b.nx - a.nx) * f,
                                a.ny + (b.ny - a.ny) * f, a.nz + (b.nz - a.nz) * f,
                                a.u + (b.u - a.u) * f, a.v + (b.v - a.v) * f);
                }
            }
            continue;
        }

        // Filled: bounding box + edge functions, both windings accepted (the contract culls nothing),
        // perspective-correct attribute interpolation via 1/w, screen-linear depth.
        double area = edge(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
        if (std::fabs(area) < 1e-9) { continue; }
        double sign = 1.0;
        if (area < 0.0) { sign = -1.0; area = -area; }
        const long x0 = std::max(0L, static_cast<long>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
        const long x1 = std::min(w - 1, static_cast<long>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
        const long y0 = std::max(0L, static_cast<long>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
        const long y1 = std::min(h - 1, static_cast<long>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));
        for (long y = y0; y <= y1; ++y) {
            for (long x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = sign * edge(v1.sx, v1.sy, v2.sx, v2.sy, px, py);  // v0's weight
                const double w1 = sign * edge(v2.sx, v2.sy, v0.sx, v0.sy, px, py);  // v1's weight
                const double w2 = sign * edge(v0.sx, v0.sy, v1.sx, v1.sy, px, py);  // v2's weight
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) { continue; }
                const double l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
                const float z = static_cast<float>(l0 * v0.sz + l1 * v1.sz + l2 * v2.sz);
                const double denom = l0 * v0.iw + l1 * v1.iw + l2 * v2.iw;
                if (denom <= 0.0) { continue; }
                const double q0 = l0 * v0.iw / denom, q1 = l1 * v1.iw / denom, q2 = l2 * v2.iw / denom;
                shade_write(x, y, z,
                            static_cast<float>(q0 * v0.nx + q1 * v1.nx + q2 * v2.nx),
                            static_cast<float>(q0 * v0.ny + q1 * v1.ny + q2 * v2.ny),
                            static_cast<float>(q0 * v0.nz + q1 * v1.nz + q2 * v2.nz),
                            static_cast<float>(q0 * v0.u + q1 * v1.u + q2 * v2.u),
                            static_cast<float>(q0 * v0.v + q1 * v1.v + q2 * v2.v));
            }
        }
    }
}

// Function-local statics dodge the static-init-order fiasco: metal-cpp registers its classes/selectors
// during static init, which calls back into sel_registerName / objc_lookUpClass below.
std::set<std::string>& sel_pool() { static std::set<std::string> s; return s; }
std::map<std::string, ClassObj*>& classes() { static std::map<std::string, ClassObj*> m; return m; }
std::vector<std::vector<Base*>>& autorelease_stack() { static std::vector<std::vector<Base*>> v; return v; }

// Stand-in kernels run with the full DispatchShape; the legacy width-only registration form is
// adapted into this signature at registration (see register_kernel below).
using DispatchShape = cheatah::gpu::metal::emulated::DispatchShape;
using Kernel = std::function<void(void**, unsigned, const DispatchShape&)>;
std::map<std::string, Kernel>& kernels() { static std::map<std::string, Kernel> k; return k; }

#if defined(CHEATAH_GPU_METAL_LEAKCHECK) && CHEATAH_GPU_METAL_LEAKCHECK
std::set<Base*>& live() { static std::set<Base*> s; return s; }
void track(Base* b) { live().insert(b); }
void untrack(Base* b) { live().erase(b); }
#else
inline void track(Base*) {}
inline void untrack(Base*) {}
#endif

void release(Base* b);  // forward: destroying an encoder releases the pass it retained

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
        case T_RENC: {
            REnc* e = static_cast<REnc*>(b);
            if (e->pass != nullptr) { release(e->pass); }  // drop the retain taken at encoder creation
            delete e;
            break;
        }
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

void register_kernel(const char* name, void (*fn)(void**, unsigned, unsigned long)) {
    // Adapt the width-only form into the shared table: a 1-D kernel sees exactly what it always
    // did — the total thread count along x — whichever dispatch form launched it.
    kernels()[name] = [fn](void** bufs, unsigned n, const DispatchShape& shape) {
        fn(bufs, n, shape.threads.width);
    };
}

void register_kernel(const char* name, void (*fn)(void**, unsigned, const DispatchShape&)) {
    kernels()[name] = fn;
}

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
                    // 4 bytes/pixel covers both emulated formats: RGBA8 and Depth32Float (a float).
                    t->data.assign(t->width * t->height * kBytesPerPixel, 0);
                    ret = reinterpret_cast<id>(t);
                } else if (s == "newRenderPipelineStateWithDescriptor:error:") {
                    // The raster IS the pipeline (the fixed interleaved-mesh contract), so the state
                    // object only needs to exist; the descriptor's functions/formats are not consulted.
                    va_arg(ap, id);                       // the descriptor
                    void** err = va_arg(ap, void**);      // NS::Error** out-param
                    if (err != nullptr) { *err = nullptr; }
                    ret = reinterpret_cast<id>(make<Base>(T_OBJ));
                } else if (s == "newDepthStencilStateWithDescriptor:" ||
                           s == "newSamplerStateWithDescriptor:") {
                    // Depth is always LESS + write and sampling always nearest in the raster — the
                    // contract's fixed state — so these are existence-only objects too.
                    ret = reinterpret_cast<id>(make<Base>(T_OBJ));
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
                    retain(e->pass);  // real Metal semantics: the encoder outlives the caller's release
                                      // of the descriptor (a draw reads the pass's attachments later)
                    // The pass's LOAD ACTIONS queue at encoder CREATION (a snapshot, as real Metal
                    // takes), so a Clear runs before any draw this encoder records — queueing at
                    // endEncoding would misorder the clear after the draws.
                    if (e->pass != nullptr && e->pass->colors != nullptr) {
                        for (auto& kv : e->pass->colors->attachments) {
                            RPCA* att = kv.second;
                            if (att->texture == nullptr || att->load_action != MTL::LoadActionClear) {
                                continue;
                            }
                            Tex* t = att->texture;
                            const std::array<unsigned char, kBytesPerPixel> px = {
                                to_unorm8(att->clear_color[0]), to_unorm8(att->clear_color[1]),
                                to_unorm8(att->clear_color[2]), to_unorm8(att->clear_color[3])};
                            c->work.push_back([t, px]() {
                                for (std::size_t i = 0; i + kBytesPerPixel <= t->data.size();
                                     i += kBytesPerPixel) {
                                    std::memcpy(t->data.data() + i, px.data(), kBytesPerPixel);
                                }
                            });
                        }
                    }
                    if (e->pass != nullptr && e->pass->depth != nullptr &&
                        e->pass->depth->texture != nullptr &&
                        e->pass->depth->load_action == MTL::LoadActionClear) {
                        Tex* t = e->pass->depth->texture;
                        const float cd = static_cast<float>(e->pass->depth->clear_depth);
                        c->work.push_back([t, cd]() {
                            float* depths = reinterpret_cast<float*>(t->data.data());
                            for (std::size_t i = 0; i < t->data.size() / sizeof(float); ++i) {
                                depths[i] = cd;
                            }
                        });
                    }
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
                    MTL::Size grid = va_arg(ap, MTL::Size);
                    MTL::Size tptg = va_arg(ap, MTL::Size);
                    // Normalize both forms to the TOTAL thread grid, like real Metal launches:
                    // dispatchThreads passes threads directly; dispatchThreadgroups passes GROUP
                    // counts, so the thread grid is groups x threadsPerThreadgroup per axis.
                    DispatchShape shape;
                    const bool by_groups = s.rfind("dispatchThreadgroups:", 0) == 0;
                    shape.threads = {by_groups ? grid.width * tptg.width : grid.width,
                                     by_groups ? grid.height * tptg.height : grid.height,
                                     by_groups ? grid.depth * tptg.depth : grid.depth};
                    shape.threads_per_threadgroup = {tptg.width, tptg.height, tptg.depth};
                    std::string fn = e->fn; auto bufs = e->bufs;
                    e->cb->work.push_back([fn, bufs, shape]() mutable {
                        auto it = kernels().find(fn);
                        if (it == kernels().end()) return;
                        unsigned long maxi = 0; for (auto& kv : bufs) if (kv.first > maxi) maxi = kv.first;
                        std::vector<void*> ptrs(maxi + 1, nullptr);
                        for (auto& kv : bufs) ptrs[kv.first] = kv.second.first->data.data() + kv.second.second;
                        it->second(ptrs.data(), static_cast<unsigned>(ptrs.size()), shape);
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
                } else if (s == "depthAttachment") {
                    if (d->depth == nullptr) {
                        d->depth = make<RPDA>(T_RPDA);
                        untrack(d->depth);  // unowned view; the descriptor frees it
                    }
                    ret = reinterpret_cast<id>(d->depth);
                }
                break;
            }
            case T_RPDA: {
                RPDA* att = static_cast<RPDA*>(b);
                if (s == "setTexture:") att->texture = reinterpret_cast<Tex*>(va_arg(ap, id));
                else if (s == "setLoadAction:") att->load_action = va_arg(ap, unsigned long);
                else if (s == "setStoreAction:") att->store_action = va_arg(ap, unsigned long);
                else if (s == "setClearDepth:") att->clear_depth = va_arg(ap, double);
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
                // Load actions were queued when the encoder was created; endEncoding has nothing left
                // to do. The set* selectors accumulate draw state, and each drawIndexedPrimitives
                // snapshots that state into a queued raster_draw (run in order at commit).
                REnc* e = static_cast<REnc*>(b);
                if (s == "setTriangleFillMode:") {
                    e->fill_mode = va_arg(ap, unsigned long);
                } else if (s == "setVertexBuffer:offset:atIndex:") {
                    Buf* bf = reinterpret_cast<Buf*>(va_arg(ap, id));
                    unsigned long off = va_arg(ap, unsigned long);
                    unsigned long idx = va_arg(ap, unsigned long);
                    e->vbufs[idx] = {bf, off};
                } else if (s == "setVertexBytes:length:atIndex:") {
                    const unsigned char* p = static_cast<const unsigned char*>(va_arg(ap, void*));
                    unsigned long len = va_arg(ap, unsigned long);
                    unsigned long idx = va_arg(ap, unsigned long);
                    e->vbytes[idx].assign(p, p + len);
                } else if (s == "setFragmentBytes:length:atIndex:") {
                    const unsigned char* p = static_cast<const unsigned char*>(va_arg(ap, void*));
                    unsigned long len = va_arg(ap, unsigned long);
                    unsigned long idx = va_arg(ap, unsigned long);
                    e->fbytes[idx].assign(p, p + len);
                } else if (s == "setFragmentTexture:atIndex:") {
                    Tex* t = reinterpret_cast<Tex*>(va_arg(ap, id));
                    unsigned long idx = va_arg(ap, unsigned long);
                    e->ftex[idx] = t;
                } else if (s == "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:") {
                    va_arg(ap, unsigned long);  // primitive type — triangles are all the raster knows
                    DrawState st;
                    st.index_count = va_arg(ap, unsigned long);
                    st.index_type = va_arg(ap, unsigned long);
                    st.ibuf = reinterpret_cast<Buf*>(va_arg(ap, id));
                    st.ioff = va_arg(ap, unsigned long);
                    st.fill_mode = e->fill_mode;
                    if (e->pass != nullptr && e->pass->colors != nullptr) {
                        auto it = e->pass->colors->attachments.find(0);
                        if (it != e->pass->colors->attachments.end()) { st.color = it->second->texture; }
                    }
                    if (e->pass != nullptr && e->pass->depth != nullptr) {
                        st.depth = e->pass->depth->texture;
                    }
                    auto vit = e->vbufs.find(1);                       // buffer(1): the stage_in vertices
                    if (vit != e->vbufs.end()) { st.vbuf = vit->second.first; st.voff = vit->second.second; }
                    auto pit = e->vbytes.find(0);                      // buffer(0): the 80-byte constant
                    if (pit != e->vbytes.end()) { st.push = pit->second; }
                    auto tit = e->ftex.find(0);                        // texture(0): the base color
                    if (tit != e->ftex.end()) { st.tex = tit->second; }
                    e->cb->work.push_back([st]() { raster_draw(st); });
                }
                // setRenderPipelineState:/setDepthStencilState:/setCullMode:/setFragmentSamplerState:
                // atIndex: carry state the raster fixes by contract (LESS+write, no cull, nearest) —
                // accepted and ignored.
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
