# Native Original Xbox build for the Helengine Xbox v0.1 cube probe.
#
# This Makefile is included into the pinned nxdk build machinery
# ($(NXDK_DIR)/Makefile). nxdk places object files beside their sources, so
# scripts/build.ps1 runs it on a staged copy of the source tree inside the
# per-invocation build directory (never on the repository worktree).
#
# Inputs (validated before any compilation):
#   NXDK_DIR           pinned nxdk checkout with built tools/libraries
#   MODE               Diagnostic | Authored         (default Diagnostic)
#   CONFIGURATION      Debug | Release               (default Debug)
#   OUTPUT_DIR         disc root (default ./disc)
#   GEN_XISO           XISO path (default ./game.iso)
#
# Diagnostic mode inputs (the native probe; no engine content involved):
#   PROBE              Clear | Triangle | Cube | AbiChecks  (default Cube)
#   STOP_AFTER_FRAMES  nonnegative integer           (default 0 = continuous)
#   POSE               Baseline | Far | NearPlane | ScreenEdge (default Baseline)
#   DISABLE_CULLING    0 | 1                         (default 0)
#   REVERSE_TRIANGLES  0 | 1                         (default 0)
#
# Authored mode inputs (an editor-built scene):
#   GENERATED_CORE_ROOT  generated Core C++ root containing helengine_core_unity.cpp
#                        (preferred) or generated_unity.cpp
#   CONTENT_ROOT         cooked content tree packaged onto the disc
#   CATALOG_ROOT         optional scene catalog sources compiled alongside Core

ifndef NXDK_DIR
$(error NXDK_DIR is required; point it at the pinned nxdk checkout)
endif

MODE ?= Diagnostic
ifeq ($(filter Diagnostic Authored,$(MODE)),)
$(error MODE must be Diagnostic or Authored (got '$(MODE)'))
endif

CONFIGURATION ?= Debug
PROBE ?= Cube
STOP_AFTER_FRAMES ?= 0
POSE ?= Baseline
DISABLE_CULLING ?= 0
REVERSE_TRIANGLES ?= 0

ifeq ($(CONFIGURATION),Debug)
DEBUG = y
HELENGINE_OPTFLAGS := -O0
else ifeq ($(CONFIGURATION),Release)
HELENGINE_OPTFLAGS := -O2
else
$(error CONFIGURATION must be Debug or Release (got '$(CONFIGURATION)'))
endif

ifeq ($(PROBE),Clear)
PROBE_VALUE := 0
else ifeq ($(PROBE),Triangle)
PROBE_VALUE := 1
else ifeq ($(PROBE),Cube)
PROBE_VALUE := 2
else ifeq ($(PROBE),AbiChecks)
PROBE_VALUE := 3
else
$(error PROBE must be Clear, Triangle, Cube or AbiChecks (got '$(PROBE)'))
endif

ifeq ($(POSE),Baseline)
POSE_VALUE := 0
else ifeq ($(POSE),Far)
POSE_VALUE := 1
else ifeq ($(POSE),NearPlane)
POSE_VALUE := 2
else ifeq ($(POSE),ScreenEdge)
POSE_VALUE := 3
else
$(error POSE must be Baseline, Far, NearPlane or ScreenEdge (got '$(POSE)'))
endif

ifeq ($(filter 0 1,$(DISABLE_CULLING)),)
$(error DISABLE_CULLING must be 0 or 1 (got '$(DISABLE_CULLING)'))
endif
ifeq ($(filter 0 1,$(REVERSE_TRIANGLES)),)
$(error REVERSE_TRIANGLES must be 0 or 1 (got '$(REVERSE_TRIANGLES)'))
endif

# Nonnegative integer: stripping digits must leave nothing, and it must not be empty.
STOP_AFTER_FRAMES_DIGITS := $(shell printf '%s' '$(STOP_AFTER_FRAMES)' | tr -cd '0-9')
ifneq ($(STOP_AFTER_FRAMES),$(STOP_AFTER_FRAMES_DIGITS))
$(error STOP_AFTER_FRAMES must be a nonnegative integer (got '$(STOP_AFTER_FRAMES)'))
endif
ifeq ($(STOP_AFTER_FRAMES),)
$(error STOP_AFTER_FRAMES must be a nonnegative integer (got empty value))
endif

XBE_TITLE = helengine-xbox-v01
OUTPUT_DIR ?= $(CURDIR)/disc
GEN_XISO ?= $(CURDIR)/game.iso
NXDK_CXX = y

# --- Authored mode inputs -----------------------------------------------------
# Validated here so a missing or mistyped root fails before anything compiles,
# rather than producing an image that silently carries no scene.
ifeq ($(MODE),Authored)
ifndef GENERATED_CORE_ROOT
$(error GENERATED_CORE_ROOT is required in Authored mode)
endif
ifndef CONTENT_ROOT
$(error CONTENT_ROOT is required in Authored mode)
endif
# An editor build emits two unity translation units. generated_unity.cpp is
# codegen's own compile-validation unit and stops at the engine sources it knew
# about when it ran. helengine_core_unity.cpp is written afterwards, during the
# finalize stage, and is the only one that also includes the project scripts,
# the generated component deserializers and the runtime/ and system/ subtrees.
# Compiling the validation unit links short by exactly those definitions, so
# prefer the finalized unit and fall back only when codegen ran standalone.
ifneq ($(wildcard $(GENERATED_CORE_ROOT)/helengine_core_unity.cpp),)
GENERATED_CORE_UNITY := $(GENERATED_CORE_ROOT)/helengine_core_unity.cpp
else ifneq ($(wildcard $(GENERATED_CORE_ROOT)/generated_unity.cpp),)
GENERATED_CORE_UNITY := $(GENERATED_CORE_ROOT)/generated_unity.cpp
else
$(error GENERATED_CORE_ROOT contains neither helengine_core_unity.cpp nor generated_unity.cpp: $(GENERATED_CORE_ROOT))
endif
ifeq ($(wildcard $(GENERATED_CORE_ROOT)/Core.hpp),)
$(error GENERATED_CORE_ROOT does not contain Core.hpp: $(GENERATED_CORE_ROOT))
endif
ifeq ($(wildcard $(CONTENT_ROOT)),)
$(error CONTENT_ROOT does not exist: $(CONTENT_ROOT))
endif
# The cooked tree must not be empty: an empty content root means the editor
# staged nothing and the image would boot with no scene to load.
CONTENT_FILE_COUNT := $(shell find '$(CONTENT_ROOT)' -type f 2>/dev/null | head -1)
ifeq ($(CONTENT_FILE_COUNT),)
$(error CONTENT_ROOT contains no files: $(CONTENT_ROOT))
endif
ifdef CATALOG_ROOT
ifeq ($(wildcard $(CATALOG_ROOT)),)
$(error CATALOG_ROOT does not exist: $(CATALOG_ROOT))
endif
CATALOG_SOURCES := $(wildcard $(CATALOG_ROOT)/*.cpp)
endif
endif

PLATFORM_DIR := $(CURDIR)/src/platform/xbox
SHADER_DIR := $(PLATFORM_DIR)/shaders

SRCS = \
	$(CURDIR)/src/main.cpp \
	$(PLATFORM_DIR)/XboxBootHost.cpp \
	$(PLATFORM_DIR)/XboxVideo.cpp \
	$(PLATFORM_DIR)/XboxCubeMesh.cpp \
	$(PLATFORM_DIR)/XboxMath.cpp \
	$(PLATFORM_DIR)/XboxRasterRenderer.cpp \
	$(PLATFORM_DIR)/XboxAbiChecks.cpp \
	$(PLATFORM_DIR)/XboxCxxAbi.cpp

SRCS += \
	$(PLATFORM_DIR)/XboxContentPath.cpp \
	$(PLATFORM_DIR)/XboxAuthoredTransform.cpp

ifeq ($(MODE),Authored)
# Only meaningful when a Core is linked: the fatal hook the generated runtime
# calls, and the content source it reads cooked assets through.
SRCS += \
	$(PLATFORM_DIR)/XboxCoreRuntimeProvider.cpp \
	$(PLATFORM_DIR)/XboxContentStreamSource.cpp \
	$(PLATFORM_DIR)/XboxRuntimeModel.cpp \
	$(PLATFORM_DIR)/XboxRuntimeMaterial.cpp
endif

SHADER_OBJS = \
	$(SHADER_DIR)/unlit_vsh.inl \
	$(SHADER_DIR)/unlit_psh.inl

HELENGINE_CXXFLAGS := \
	-std=c++17 -Wall -Wextra -Werror $(HELENGINE_OPTFLAGS) \
	-I$(CURDIR)/src \
	-DHELENGINE_XBOX_PROBE=$(PROBE_VALUE) \
	-DHELENGINE_XBOX_STOP_AFTER_FRAMES=$(STOP_AFTER_FRAMES)u \
	-DHELENGINE_XBOX_POSE=$(POSE_VALUE) \
	-DHELENGINE_XBOX_DISABLE_CULLING=$(DISABLE_CULLING) \
	-DHELENGINE_XBOX_REVERSE_TRIANGLES=$(REVERSE_TRIANGLES)

ifeq ($(MODE),Authored)
# Tell the platform sources a generated Core is present so the host can drive it
# instead of the native probe geometry.
#
# HE_CPP_RUNTIME_PROVIDER_HEADER routes the generated runtime's fatal path to
# he_cpp_custom::Fail in XboxCoreRuntimeProvider.cpp. Without it a Core
# generated with codegen-use-exceptions=false lowers every managed throw to
# std::abort(), which kills the console and discards the reason. With std
# string, vector, unordered_map, unordered_set and chrono all enabled the
# provider contract narrows to Fail alone, so no HeCpp* alias leaves its
# standard type.
#
# The define has to reach the generated Core and the platform sources alike:
# both include native_runtime.hpp, and disagreeing about the provider would put
# two different definitions of the runtime's inline failure path in one binary.
# Angle-bracket form on purpose: the value has to survive make, the shell and
# the compiler's command line intact, and a quoted include would need escaping
# at every one of those layers. src is already on the include path.
HELENGINE_CORE_PROVIDER_HEADER := <platform/xbox/XboxCoreRuntimeProvider.hpp>
#
# The generated tree comes in through -isystem, not -I. It is not written to
# this repository's warning bar - native_exceptions.hpp alone trips
# -Wunused-parameter three times - and the alternative, dropping -Werror for
# every platform source that touches a Core header, would give up the bar on
# this repository's own code to accommodate generated code. -isystem suppresses
# diagnostics from those headers and leaves quoted includes resolving as before.
HELENGINE_CXXFLAGS += -DHELENGINE_XBOX_HAS_GENERATED_CORE=1 \
	-DHE_CPP_RUNTIME_PROVIDER_HEADER='$(HELENGINE_CORE_PROVIDER_HEADER)' \
	-isystem $(GENERATED_CORE_ROOT) -isystem $(GENERATED_CORE_ROOT)/runtime
ifdef CATALOG_ROOT
HELENGINE_CXXFLAGS += -I$(CATALOG_ROOT)
endif
endif

# `override` so command-line CXXFLAGS/LDFLAGS cannot silently drop these.
override CXXFLAGS += $(HELENGINE_CXXFLAGS)
override LDFLAGS += -map:$(CURDIR)/main.map

include $(NXDK_DIR)/Makefile

ifeq ($(MODE),Authored)
# The generated Core is one large unity translation unit. It is compiled here
# rather than added to SRCS because nxdk writes each object beside its source,
# and the generated root is mounted read-only; objects therefore land in the
# invocation directory instead.
#
# Its flags deliberately differ from the platform sources:
#   -std=c++20  generated code uses C++20 library facilities
#   -w          generated code is not written to this repository's warning bar,
#               and -Werror would fail the build on cosmetic diagnostics
#   RTTI on     generic implementation dispatch selects concrete generated types
#               through dynamic_cast; __RTDynamicCast comes from XboxCxxAbi.cpp
#   exceptions  left at nxdk's -fno-exceptions default, matching a Core
#               generated with codegen-use-exceptions=false
GENERATED_CORE_OBJECT := $(CURDIR)/generated_core_unity.obj
GENERATED_CORE_CXXFLAGS := -std=c++20 -w $(HELENGINE_OPTFLAGS) \
	-DHE_CPP_RUNTIME_PROVIDER_HEADER='$(HELENGINE_CORE_PROVIDER_HEADER)' \
	-I$(GENERATED_CORE_ROOT) -I$(GENERATED_CORE_ROOT)/runtime -I$(CURDIR)/src

$(GENERATED_CORE_OBJECT): $(GENERATED_CORE_UNITY)
	@echo "[ CXX CORE ] $@"
	$(VE)$(CXX) $(NXDK_CXXFLAGS) $(GENERATED_CORE_CXXFLAGS) -c -o '$@' '$<'

AUTHORED_OBJECTS := $(GENERATED_CORE_OBJECT)

ifdef CATALOG_ROOT
CATALOG_OBJECTS := $(patsubst $(CATALOG_ROOT)/%.cpp,$(CURDIR)/catalog_%.obj,$(CATALOG_SOURCES))
$(CURDIR)/catalog_%.obj: $(CATALOG_ROOT)/%.cpp
	@echo "[ CXX CAT  ] $@"
	$(VE)$(CXX) $(NXDK_CXXFLAGS) $(GENERATED_CORE_CXXFLAGS) -c -o '$@' '$<'
AUTHORED_OBJECTS += $(CATALOG_OBJECTS)
endif

# nxdk links main.exe from all of its prerequisites, so naming the authored
# objects here adds them to the link without redefining its recipe.
main.exe: $(AUTHORED_OBJECTS)

# Copy the cooked tree next to default.xbe so extract-xiso packs it into the
# image. This is a prerequisite of the XISO rule nxdk already defines.
.PHONY: stage-authored-content
stage-authored-content: | $(OUTPUT_DIR)
	@echo "[ CONTENT  ] $(CONTENT_ROOT) -> $(OUTPUT_DIR)"
	$(VE)cp -R '$(CONTENT_ROOT)/.' '$(OUTPUT_DIR)/'

$(GEN_XISO): stage-authored-content

CLEANRULES += clean-authored
.PHONY: clean-authored
clean-authored:
	$(VE)rm -f $(AUTHORED_OBJECTS)
endif

# Hand-written shader sources compiled with the pinned nxdk tools only (no cgc):
#   unlit.vsh  NV_vertex_program 1.1 text  -> vp20compiler -> NV2A microcode dwords
#   unlit.psh  RC1.0 register-combiner text -> fp20compiler -> pbkit push calls
$(SHADER_DIR)/unlit_vsh.inl: $(SHADER_DIR)/unlit.vsh $(VP20COMPILER)
	@echo "[ VP20     ] $@"
	$(VE)$(VP20COMPILER) '$<' > '$@' || { rm -f '$@'; exit 1; }
	@test -s '$@'

$(SHADER_DIR)/unlit_psh.inl: $(SHADER_DIR)/unlit.psh $(FP20COMPILER)
	@echo "[ FP20     ] $@"
	$(VE)$(FP20COMPILER) '$<' > '$@' || { rm -f '$@'; exit 1; }
	@test -s '$@'

.PHONY: print-config
print-config:
	@echo "MODE=$(MODE) CONFIGURATION=$(CONFIGURATION) PROBE=$(PROBE) ($(PROBE_VALUE)) STOP_AFTER_FRAMES=$(STOP_AFTER_FRAMES) POSE=$(POSE) ($(POSE_VALUE)) DISABLE_CULLING=$(DISABLE_CULLING) REVERSE_TRIANGLES=$(REVERSE_TRIANGLES)"
	@echo "NXDK_DIR=$(NXDK_DIR) OUTPUT_DIR=$(OUTPUT_DIR) GEN_XISO=$(GEN_XISO)"
	@echo "GENERATED_CORE_ROOT=$(GENERATED_CORE_ROOT) CONTENT_ROOT=$(CONTENT_ROOT) CATALOG_ROOT=$(CATALOG_ROOT)"
