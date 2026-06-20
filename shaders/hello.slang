// hello.slang — a minimal Slang compute shader (OUTLINE / smoke artifact).
//
// This is the "it works" shader: scripts/doctor.sh compiles it with `slangc` to SPIR-V and
// validates it with `spirv-val`, proving the Slang -> SPIR-V toolchain is healthy end-to-end
// before any real GPU work. It doubles as the template a user starts from.
//
// Build manually:
//   slangc shaders/hello.slang -target spirv -entry main -o hello.spv && spirv-val hello.spv
//
// It fills an output buffer with f(i) = i * 2 — a stand-in for a real cheatah compute kernel.
// The [numthreads(64,...)] local size pairs with gpu.dispatch.group_count_1d(n, 64) on the host.

// A plain storage buffer, accessed bindlessly once descriptor indexing is wired in gpu.vulkan.
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = tid.x * 2u;
}
