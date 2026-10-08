cbuffer TransformBuffer : register(b0) { row_major float4x4 WorldViewProjection; };
cbuffer BaseColorBuffer : register(b1) { float4 BaseColor; };
struct VSInput { float3 position : POSITION; };
struct VSOutput { float4 position : SV_Position; };
VSOutput VS(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    return output;
}
float4 PS(VSOutput input) : SV_Target { return BaseColor; }
