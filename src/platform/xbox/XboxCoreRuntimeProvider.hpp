#pragma once

// Fatal-failure hook for the generated Core.
//
// This Core is generated with codegen-use-exceptions=false, so the runtime
// lowers every managed throw site to he_cpp_runtime_detail::Raise, which is
// [[noreturn]]. With no provider configured that lands in DefaultFail, which
// calls std::abort() and discards the message: on a console that is an
// instant, silent death with nothing to read back. Every reason a scene can
// fail to load - a missing cooked asset, an unregistered component, a
// malformed record - arrives through this one function, so it is the only
// place a diagnostic can be attached.
//
// csharpcodegen already exposes HE_CPP_RUNTIME_PROVIDER_HEADER for exactly
// this, and it costs nothing generic: with std string, vector, unordered_map,
// unordered_set and chrono all enabled (see helcpp_config.hpp) the provider
// contract narrows to Fail alone, and every HeCpp* alias stays on its standard
// type. Nothing else in the generated tree changes.
//
// This header is included from inside runtime/native_runtime.hpp, before any
// generated type exists, so it must stay self-contained: no Core headers, no
// engine headers, and no dependency on anything the runtime has not yet
// declared.

namespace he_cpp_custom {

// Records the message where a post-mortem can find it, reports it through the
// same two sinks as the rest of the native probe, paints the failure colour so
// the outcome is legible without a debugger attached, and halts. Never returns.
[[noreturn]] void Fail(const char* message);

} // namespace he_cpp_custom

namespace helengine::xbox {

// Last message passed to he_cpp_custom::Fail, or an empty string if Core has
// not failed. Readable from a debugger or an emulator memory probe.
const char* LastCoreFailureMessage();

// Nonzero once Fail has been entered. Separate from the message so a probe can
// distinguish "Core failed" from "Core failed with an empty message".
unsigned CoreFailureCount();

} // namespace helengine::xbox
