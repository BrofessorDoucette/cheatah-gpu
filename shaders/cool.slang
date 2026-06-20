// cool.slang — an animated fullscreen plasma, written once in Slang and compiled NATIVELY to either
// Vulkan (SPIR-V) or Apple (Metal) from this same source, so the window system test draws the same
// thing on either backend without the user thinking about it. Used by the optional window test
// (systests/test_window.purr) and validated by scripts/doctor.sh + the shader compile step.

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

// A fullscreen triangle straight from the vertex id — no vertex buffer needed.
[shader("vertex")]
VOut vertexMain(uint vid: SV_VertexID) {
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    VOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = p;
    return o;
}

// An animated plasma — `time` arrives as a push constant the host updates each frame.
[shader("fragment")]
float4 fragmentMain(float2 uv: TEXCOORD0, uniform float time) : SV_Target {
    float2 p = uv * 6.2831853;
    float v = sin(p.x + time) + sin(p.y + time * 1.3) + sin((p.x + p.y) * 0.5 + time);
    float3 col = 0.5 + 0.5 * cos(float3(0.0, 2.0, 4.0) + v + time);
    return float4(col, 1.0);
}
