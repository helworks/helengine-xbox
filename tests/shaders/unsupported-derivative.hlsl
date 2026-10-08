float4 PSMain(float4 colour : COLOR0) : SV_Target {
    return ddx(colour);
}
