cbuffer Transform : register(b0) { row_major float4x4 WorldViewProjection; };
Texture2D Albedo : register(t0);
SamplerState AlbedoSampler : register(s0);
struct VSInput { float3 position : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.colour = input.colour;
    output.uv = input.uv;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target {
    return Albedo.Sample(AlbedoSampler, input.uv) * input.colour;
}
