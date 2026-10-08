#!/bin/sh
set -eu
# Run in the pinned SDK with the repository at /src and generated Core supplied.
core="$1"
g++ -std=c++20 -ffunction-sections -fdata-sections -Wl,--gc-sections -I/src/src -I"$core" \
 /src/tests/generated_rounded_batch_tests.cpp "$core"/Batch2DWriter.cpp "$core"/Batch2DBuffer.cpp \
 "$core"/Batch2DRun.cpp "$core"/Batch2DVertex.cpp "$core"/Batch2DGeometry.cpp \
 "$core"/RuntimeTexture.cpp "$core"/RuntimeData.cpp "$core"/float4.cpp "$core"/float4x4.cpp \
 -o /tmp/generated_rounded_batch_tests
/tmp/generated_rounded_batch_tests
