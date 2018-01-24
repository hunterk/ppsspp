LOCAL_PATH := $(call my-dir)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

COREFLAGS  :=
CORE_DIR   := ../..
FFMPEGDIR  := $(CORE_DIR)/ffmpeg
FFMPEGLIBS += libavformat libavcodec libavutil libswresample libswscale

ifeq ($(TARGET_ARCH),arm64)
  COREFLAGS += -DARM64 -D_ARCH_64
  HAVE_NEON := 1
  WITH_DYNAREC = arm64
  FFMPEGLIBDIR := $(FFMPEGDIR)/android/arm64/lib
  FFMPEGINCFLAGS := -I$(FFMPEGDIR)/android/arm64/include
endif

ifeq ($(TARGET_ARCH),arm)
  COREFLAGS += -DARM -DARMEABI_V7A -D__arm__ -DARM_ASM -D_ARCH_32 -mfpu=neon
  HAVE_NEON := 1
  WITH_DYNAREC = arm
  FFMPEGLIBDIR := $(FFMPEGDIR)/android/armv7/lib
  FFMPEGINCFLAGS := -I$(FFMPEGDIR)/android/armv7/include
endif

ifeq ($(TARGET_ARCH),x86)
  COREFLAGS += -D_ARCH_32 -D_M_IX86 -fomit-frame-pointer -mtune=atom -mfpmath=sse -mssse3 -mstackrealign
  WITH_DYNAREC = x86
  FFMPEGLIBDIR := $(FFMPEGDIR)/android/x86/lib
  FFMPEGINCFLAGS := -I$(FFMPEGDIR)/android/x86/include
endif

ifeq ($(TARGET_ARCH),x86_64)
  COREFLAGS += -D_ARCH_64 -D_M_X64 -fomit-frame-pointer -mtune=atom -mfpmath=sse -mssse3 -mstackrealign
  WITH_DYNAREC = x86_64
  FFMPEGLIBDIR := $(FFMPEGDIR)/android/x86_64/lib
  FFMPEGINCFLAGS := -I$(FFMPEGDIR)/android/x86_64/include
endif

include $(CLEAR_VARS)
LOCAL_MODULE    := libavformat
LOCAL_SRC_FILES := $(FFMPEGLIBDIR)/libavformat.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libavcodec
LOCAL_SRC_FILES := $(FFMPEGLIBDIR)/libavcodec.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libavutil
LOCAL_SRC_FILES := $(FFMPEGLIBDIR)/libavutil.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libswresample
LOCAL_SRC_FILES := $(FFMPEGLIBDIR)/libswresample.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libswscale
LOCAL_SRC_FILES := $(FFMPEGLIBDIR)/libswscale.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

PLATFORM_EXT   := android
platform := android
GLES = 1

LOCAL_MODULE := retro

include $(CORE_DIR)/libretro/Makefile.common

COREFLAGS += -DINLINE="inline" -DPPSSPP -DUSE_FFMPEG -DMOBILE_DEVICE -DBAKE_IN_GIT -DDYNAREC -D__LIBRETRO__ -DUSING_GLES2 -D__STDC_CONSTANT_MACROS -DGLEW_NO_GLU -DNO_VULKAN $(INCFLAGS)
LOCAL_SRC_FILES = $(SOURCES_CXX) $(SOURCES_C) $(ASMFILES)
LOCAL_CPPFLAGS := -fexceptions -Wall -std=gnu++11 -Wno-literal-suffix $(COREFLAGS)
LOCAL_CFLAGS := -O2 -ffast-math -DANDROID $(COREFLAGS)
LOCAL_LDLIBS += -lz -llog -lGLESv2 -lEGL -latomic
LOCAL_STATIC_LIBRARIES += $(FFMPEGLIBS)

include $(BUILD_SHARED_LIBRARY)

