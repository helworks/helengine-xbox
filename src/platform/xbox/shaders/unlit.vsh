!!VP1.1
# Helengine Xbox v0.1 unlit vertex program.
# Hand-written NV_vertex_program 1.1 text compiled by the pinned nxdk
# vp20compiler (no NVIDIA cgc dependency). VP1.1 is required for DPH and RCC.
#
# Attribute slots (NV2A vertex data arrays):
#   v[0] = position, 3 floats; DPH supplies the homogeneous w = 1
#   v[3] = diffuse colour, 4 floats (interpolated unlit output)
#
# Constant layout (vp20compiler adds 96 to every c[N]; the renderer therefore
# uploads with NV097_SET_TRANSFORM_CONSTANT_LOAD = 96):
#   c[0..3] = rows of clip = Projection * View * Model (row-major, column-vector)
#   c[4]    = viewport scale  (width/2, -height/2, 16777215, 0)
#   c[5]    = viewport offset (0.53125 + width/2, 0.53125 + height/2, 0, 0)
#
# Output convention (matches pbkit's samples and xemu's program-mode path):
#   o[HPOS].xyz = screen-space x,y in pixels and z in 24-bit depth units,
#                 already divided by clip w; o[HPOS].w = clip w.
#   Perspective division happens here (RCC = clamped reciprocal); the viewport
#   conversion happens here too. pbkit's viewport registers hold the same
#   values but are not re-applied to program outputs.
DPH R0.x, v[0], c[0];
DPH R0.y, v[0], c[1];
DPH R0.z, v[0], c[2];
DPH R0.w, v[0], c[3];
RCC R1.x, R0.w;
MUL R0.xyz, R0, R1.x;
# One constant register per instruction (NV_vertex_program rule), so the
# viewport scale and offset are applied in two steps.
MUL R0.xyz, R0, c[4];
ADD R0.xyz, R0, c[5];
MOV o[HPOS], R0;
MOV o[COL0], v[3];
END
