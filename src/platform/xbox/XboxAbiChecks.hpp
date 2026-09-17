#pragma once

// Runtime verification for the MSVC-ABI dynamic_cast support in XboxCxxAbi.cpp.
//
// These run on the console rather than the host because the RTTI layout being
// walked is produced by clang targeting i386-pc-win32, which the development
// host's compiler does not emit. A host-side test would exercise a different
// ABI and prove nothing about the target.

namespace helengine::xbox {

struct AbiCheckResults {
    static constexpr unsigned MaxNameLength = 48;

    unsigned Passed = 0;
    unsigned Failed = 0;
    char FirstFailure[MaxNameLength] = "";

    bool AllPassed() const { return Failed == 0; }
};

// Exercises pointer downcasts, rejected casts, cross-casts through multiple
// inheritance, and virtual-base displacement. Allocates nothing.
AbiCheckResults RunDynamicCastChecks();

} // namespace helengine::xbox
