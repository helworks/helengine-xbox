#pragma once
// The generated shader interfaces are present only in the explicitly enabled profile.
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE) && __has_include("IShaderRenderManager3D.hpp")
#define HELENGINE_XBOX_HAS_SHADER_RUNTIME 1
#endif
