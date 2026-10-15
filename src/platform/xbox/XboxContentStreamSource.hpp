#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Serves generated Core content reads from the DVD.
//
// The generated scene catalog manifest hands Core unrooted, forward-slashed
// relative paths such as "cooked/scenes/rendering/cube_test.hasset". nxdk maps
// the directory holding default.xbe to D:, and the builder stages the cooked
// tree beside the executable, so the mapping is a prefix and a separator swap.
// The rules for that live in XboxContentPath, which has no Core dependency and
// is exercised by the host contract tests.
//
// Nothing here throws: the Core is generated with codegen-use-exceptions=false
// and nxdk compiles -fno-exceptions. A read that cannot be served logs the
// concrete reason and returns null, which Core turns into a managed failure.
// The log line is what names the file; the failure that follows will not.

#include <cstddef>
#include <string>

#include "IContentStreamSource.hpp"

namespace helengine::xbox {

class XboxContentStreamSource final : public ::IContentStreamSource {
public:
    // A 2048x2048 RGBA authored texture takes 16 MiB plus its serialized header.
    // Keep a finite bound while allowing the menu's background texture.
    static constexpr std::size_t MaxAssetBytes = 32u * 1024u * 1024u;

    explicit XboxContentStreamSource(const char* contentRootPath = "D:\\");

    // Returns a memory-backed stream owning its own copy of the file, or null.
    ::Stream* OpenRead(HeCppString assetPath) override;

    // Counters and the last failure reason are readable without a debugger and
    // keep the host's report honest about what was actually served.
    unsigned ServedCount() const { return servedCount_; }
    unsigned FailureCount() const { return failureCount_; }
    const char* LastFailure() const { return lastFailure_; }

private:
    std::string contentRootPath_;
    unsigned servedCount_ = 0;
    unsigned failureCount_ = 0;
    const char* lastFailure_ = "";

    // Records reason, counts the failure and returns null so call sites stay
    // single-expression.
    ::Stream* fail(const char* reason, const char* detail);
};

} // namespace helengine::xbox

#endif
