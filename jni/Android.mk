# SPDX-License-Identifier: GPL-3.0-or-later
LOCAL_PATH := $(call my-dir)
ROOT := $(LOCAL_PATH)/..

include $(ROOT)/sources.mk

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := \
	$(addprefix ../upstream/portalib/,$(PORTALIB_SRCS)) \
	$(addprefix ../upstream/src/,$(CORE_SRCS)) \
	$(addprefix ../src/,$(GLUE_SRCS))
LOCAL_C_INCLUDES := $(addprefix $(ROOT)/,$(XROAR_INCLUDES))
LOCAL_CFLAGS    := $(XROAR_DEFINES) -std=gnu11 -O2 -fvisibility=hidden \
                   -Wno-unused-function -Werror=implicit-function-declaration
LOCAL_LDFLAGS   := -Wl,--version-script=$(ROOT)/link.T
LOCAL_LDLIBS    := -lm -llog
LOCAL_ARM_MODE  := arm
include $(BUILD_SHARED_LIBRARY)
