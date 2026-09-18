#pragma once

// Resolution of manifest-relative content paths to Xbox disc paths.
//
// Deliberately free of any generated-Core dependency so the rules can be
// compiled and exercised by the host contract tests, which never see nxdk or
// the generated tree. XboxContentStreamSource is the only production caller.

#include <string>

namespace helengine::xbox {

// Returns null when the path is a plain relative content path, or a short
// static reason when it is not. The generated scene catalog manifest only ever
// emits such paths, so a deviation means a corrupt manifest or a cooked record
// pointing somewhere it should not; either is worth refusing rather than
// following onto another drive or out of the content tree.
const char* ValidateRelativeContentPath(const std::string& path);

// Joins a validated relative path onto the content root and converts the
// manifest's forward slashes to Xbox separators. Returns false and sets reason
// when the path is rejected; reason is null on success.
bool ResolveDiscContentPath(const char* contentRootPath,
                            const std::string& assetPath,
                            std::string& resolved,
                            const char*& reason);

} // namespace helengine::xbox
