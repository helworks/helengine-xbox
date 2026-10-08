#pragma once
#include "Batch2DWriter.hpp"
#include "Batch2DGeometry.hpp"
#include <cmath>
namespace helengine::xbox {
inline bool EmitRoundedTexturedQuad(::Batch2DWriter& writer, ::Batch2DRun run,
    double x, double y, double width, double height, double rotation) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(rotation) || width <= 0 || height <= 0 || !run.Texture ||
        run.Variant != ::Batch2DVariant::Textured) return false;
    ::Batch2DVertex tl, tr, br, bl;
    ::Batch2DGeometry::CreateQuad__out8_out9_out10_out11(x,y,width,height,0.,rotation,
        ::float4(0,0,1,1), ::float4(1,1,1,1), tl,tr,br,bl);
    writer.AppendQuad(run,tl,tr,br,bl);
    return true;
}
}
