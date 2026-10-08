#include "platform/xbox/XboxRenderManager3D.hpp"
#include "IShaderRenderManager3D.hpp"
#include <type_traits>
static_assert(std::is_base_of<IShaderRenderManager3D, helengine::xbox::XboxRenderManager3D>::value,
              "Authored Xbox renderer cannot receive generated shader materials");
static_assert(!std::is_abstract<helengine::xbox::XboxRenderManager3D>::value,
              "Authored Xbox shader callbacks must have concrete implementations");
