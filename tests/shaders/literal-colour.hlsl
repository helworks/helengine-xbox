cbuffer Transform : register(b0) { row_major float4x4 WorldViewProjection; };
struct VSInput { float3 position : POSITION; float4 colour : COLOR0; };
struct VSOutput { float4 position : SV_Position; float4 colour : COLOR0; };
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.colour = input.colour * float4(0.25, 0.5, 0.75, 0.875);
    return output;
}
float4 PSMain(VSOutput input) : SV_Target { return input.colour; }
