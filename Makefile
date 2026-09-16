# Native Original Xbox build for the Helengine Xbox v0.1 cube probe.
#
# This Makefile is included into the pinned nxdk build machinery
# ($(NXDK_DIR)/Makefile). nxdk places object files beside their sources, so
# scripts/build.ps1 runs it on a staged copy of the source tree inside the
# per-invocation build directory (never on the repository worktree).
#
# Inputs (validated before any compilation):
#   NXDK_DIR           pinned nxdk checkout with built tools/libraries
#   CONFIGURATION      Debug | Release              (default Debug)
#   PROBE              Clear | Triangle | Cube      (default Cube)
#   STOP_AFTER_FRAMES  nonnegative integer           (default 0 = continuous)
#   POSE               Baseline | Far | NearPlane | ScreenEdge (default Baseline)
#   DISABLE_CULLING    0 | 1                         (default 0)
#   REVERSE_TRIANGLES  0 | 1                         (default 0)
#   OUTPUT_DIR         disc root; contains only default.xbe (default ./disc)
#   GEN_XISO           XISO path (default ./game.iso)

ifndef NXDK_DIR
$(error NXDK_DIR is required; point it at the pinned nxdk checkout)
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
else
$(error PROBE must be Clear, Triangle or Cube (got '$(PROBE)'))
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

PLATFORM_DIR := $(CURDIR)/src/platform/xbox
SHADER_DIR := $(PLATFORM_DIR)/shaders

SRCS = \
	$(CURDIR)/src/main.cpp \
	$(PLATFORM_DIR)/XboxBootHost.cpp \
	$(PLATFORM_DIR)/XboxVideo.cpp \
	$(PLATFORM_DIR)/XboxCubeMesh.cpp \
	$(PLATFORM_DIR)/XboxMath.cpp \
	$(PLATFORM_DIR)/XboxRasterRenderer.cpp

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

# `override` so command-line CXXFLAGS/LDFLAGS cannot silently drop these.
override CXXFLAGS += $(HELENGINE_CXXFLAGS)
override LDFLAGS += -map:$(CURDIR)/main.map

include $(NXDK_DIR)/Makefile

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
	@echo "CONFIGURATION=$(CONFIGURATION) PROBE=$(PROBE) ($(PROBE_VALUE)) STOP_AFTER_FRAMES=$(STOP_AFTER_FRAMES) POSE=$(POSE) ($(POSE_VALUE)) DISABLE_CULLING=$(DISABLE_CULLING) REVERSE_TRIANGLES=$(REVERSE_TRIANGLES)"
	@echo "NXDK_DIR=$(NXDK_DIR) OUTPUT_DIR=$(OUTPUT_DIR) GEN_XISO=$(GEN_XISO)"
