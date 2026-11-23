# SPDX-License-Identifier: GPL-3.0-or-later
#
#   make            build xroar_libretro.so for the host (desktop testing)
#   make test       smoke-test the core without a frontend
#   make android    build the armeabi-v7a core with the Android NDK
#                   (needs ndk-build on PATH or ANDROID_NDK_HOME set)
#   make clean
#
# No ./configure step: src/config.h replaces XRoar's generated config.h.

include sources.mk

BUILD    := build
TARGET   := xroar_libretro.so

CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -fPIC -std=gnu11 -Wall -Wno-unused-function -fvisibility=hidden \
            -Werror=implicit-function-declaration
CPPFLAGS += $(XROAR_DEFINES) $(addprefix -I,$(XROAR_INCLUDES))
LDFLAGS  += -shared -Wl,--no-undefined -Wl,--version-script=link.T
LDLIBS   += -lm -lpthread

PORTALIB_OBJS := $(addprefix $(BUILD)/portalib/,$(PORTALIB_SRCS:.c=.o))
CORE_OBJS     := $(addprefix $(BUILD)/core/,$(CORE_SRCS:.c=.o))
GLUE_OBJS     := $(addprefix $(BUILD)/glue/,$(GLUE_SRCS:.c=.o))
OBJS := $(PORTALIB_OBJS) $(CORE_OBJS) $(GLUE_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS) link.T
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/portalib/%.o: upstream/portalib/%.c src/config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/core/%.o: upstream/src/%.c src/config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/glue/%.o: src/%.c src/config.h src/libretro_host.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

SMOKE := $(BUILD)/smoke
$(SMOKE): tests/smoke.c $(TARGET)
	$(CC) -Isrc -O1 -g -o $@ tests/smoke.c -ldl

test: $(SMOKE)
	sh tests/run-smoke.sh $(SMOKE) ./$(TARGET)

NDK_BUILD := $(if $(ANDROID_NDK_HOME),$(ANDROID_NDK_HOME)/ndk-build,ndk-build)
android:
	$(NDK_BUILD) NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -j4

clean:
	rm -rf build libs obj jni/libs jni/obj xroar_libretro.so

.PHONY: all test android clean
