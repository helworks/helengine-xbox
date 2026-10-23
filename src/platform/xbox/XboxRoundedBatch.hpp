#pragma once
#include "Batch2DWriter.hpp"
#include "Batch2DGeometry.hpp"
#include "runtime/native_real.hpp"
namespace helengine::xbox {
/// Keeps configured evaluation arithmetic until shared geometry writes fixed-width GPU vertices.
inline bool EmitRoundedTexturedQuad(::Batch2DWriter& writer, ::Batch2DRun run,
    ::RealEvaluation x, ::RealEvaluation y, ::RealEvaluation width, ::RealEvaluation height, ::RealEvaluation rotation) {
    if (!::RealEvaluation::IsFinite(x) || !::RealEvaluation::IsFinite(y) ||
        !::RealEvaluation::IsFinite(width) || !::RealEvaluation::IsFinite(height) ||
        !::RealEvaluation::IsFinite(rotation) || width <= 0 || height <= 0 || !run.Texture ||
        run.Variant != ::Batch2DVariant::Textured) return false;
    ::Batch2DVertex tl, tr, br, bl;
    ::Batch2DGeometry::CreateQuad__out8_out9_out10_out11(x,y,width,height,0.,rotation,
        ::float4(0,0,1,1), ::float4(1,1,1,1), tl,tr,br,bl);
    writer.AppendQuad(run,tl,tr,br,bl);
    return true;
}
}
