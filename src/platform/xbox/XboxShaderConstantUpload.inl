// Included by the renderer and by the packet-level host test. pbkit entry
// points, kConstantBase and XboxShaderConstantCount are supplied by the caller.
void PushCompiledConstants(void*, const float (*constants)[4]) {
    // Incrementing constant methods span 0xb80..0xbfc (8 float4 rows).
    // A longer packet reaches back-light registers beginning at 0xc00.
    for (unsigned first = 0; first < XboxShaderConstantCount; first += 8) {
        uint32_t* p = pb_begin();
        p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, kConstantBase + first);
        pb_push(p++, NV097_SET_TRANSFORM_CONSTANT, 32);
        std::memcpy(p, constants[first], 32 * sizeof(float));
        p += 32;
        pb_end(p);
    }
}
