.DEFAULT_GOAL := all

ROOT_DIR := $(CURDIR)
BUILD_DIR ?= build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj/app
BIN_DIR := $(BUILD_DIR)/bin
THIRDPARTY_DIR := 3rdparty
THIRDPARTY_SRC := $(THIRDPARTY_DIR)/open_src
THIRDPARTY_INSTALL := $(THIRDPARTY_DIR)/install
DEBUG_DIR ?= debug
RELEASE_DIR ?= release
RELEASE_VERSION ?= 1.0.0
RELEASE_PROFILE ?= web-only

# Include HiSilicon toolchain (sets CROSS_COMPILE, CXX, CPU_FLAGS,
# HISI_DEFINES, HISI_MPP_STATIC_LIBS, LDFLAGS, LDLIBS, etc.)
# include mk/toolchain_hi3516dv300.mk
include $(ROOT_DIR)/libs/hisi_vendor/toolchain_hi3516dv300.mk

ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
endif

OPENSSL_LIBS := $(THIRDPARTY_INSTALL)/lib/libssl.a $(THIRDPARTY_INSTALL)/lib/libcrypto.a
SRTP_LIBS := $(THIRDPARTY_INSTALL)/lib/libsrtp2.a
THIRDPARTY_LIBS := $(SRTP_LIBS) $(OPENSSL_LIBS)
SYSUPGRADE_LDFLAGS ?= -static

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-date-time
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += $(CPU_FLAGS)
CXXFLAGS += $(HISI_DEFINES)
CXXFLAGS += -DLIVE_STREAM_ENABLE_HISI_MPP
CXXFLAGS += -Iapp
CXXFLAGS += -Ilibs/infra/include
CXXFLAGS += -Ilibs/logger/include
CXXFLAGS += -Ilibs/config/include
CXXFLAGS += -Ilibs/auth/include
CXXFLAGS += -Ilibs/event/include
CXXFLAGS += -Ilibs/system/include
CXXFLAGS += -Ilibs/network_config/include
CXXFLAGS += -Ilibs/network_config/src
CXXFLAGS += -Ilibs/media_codec/include
CXXFLAGS += -Ilibs/rtp/include
CXXFLAGS += -Ilibs/net/include
CXXFLAGS += -Ilibs/net_adaptive/include
CXXFLAGS += -Ilibs/ai/include
CXXFLAGS += -Ilibs/device_media/include
CXXFLAGS += -Ilibs/media_source/include
CXXFLAGS += -Ilibs/media_pipeline/include
CXXFLAGS += -Ilibs/hisi_vendor/include
CXXFLAGS += -Ilibs/region/include
CXXFLAGS += -Ilibs/rtsp/include
CXXFLAGS += -Ilibs/webrtc/include
CXXFLAGS += -Ilibs/snapshot/include
CXXFLAGS += -Ilibs/onvif/include
CXXFLAGS += -Ilibs/alarm/include
CXXFLAGS += -Ilibs/http/include
CXXFLAGS += -Ilibs/http_media/include
CXXFLAGS += -I$(THIRDPARTY_INSTALL)/include
CXXFLAGS += -I$(THIRDPARTY_SRC)/openssl-1.1.1w/include
CXXFLAGS += -I$(THIRDPARTY_SRC)
CXXFLAGS += -I$(HISI_MPP_INC)
CXXFLAGS += -pthread

MODULES := \
	infra \
	logger \
	net \
	config \
	event \
	auth \
	system \
	network_config \
	ai \
	hisi_vendor \
	device_media \
	rtp \
	net_adaptive \
	media_source \
	media_pipeline \
	region \
	rtsp \
	webrtc \
	snapshot \
	onvif \
	alarm \
	http_media \
	http \
	media_codec

MODULE_LIBS :=
APP_SRCS := \
	app/runtime/main.cpp \
	app/runtime/app_runtime.cpp \
	app/runtime/runtime_paths.cpp \
	app/subsystems/core_subsystem.cpp \
	app/subsystems/device_subsystem.cpp \
	app/subsystems/media_subsystem.cpp \
	app/subsystems/protocol_options.cpp \
	app/subsystems/protocol_runtime_updates.cpp \
	app/subsystems/protocol_subsystem.cpp \
	app/config/protocol_runtime_config.cpp \
	app/config/runtime_config.cpp \
	app/platform/linux/linux_network_platform.cpp \
	app/platform/linux/linux_platform_common.cpp \
	app/platform/linux/linux_system_platform.cpp \
	app/platform/linux/linux_time_platform.cpp \
	app/platform/linux/platform_factory.cpp \
	app/platform/linux/upgrade_platform.cpp \
	app/tools/sysupgrade/upgrade_flash.cpp
APP_OBJS := $(patsubst app/%.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))
APP_DEPS := $(APP_OBJS:.o=.d)
SYSUPGRADE_SRCS := \
	app/tools/sysupgrade/live_sysupgrade.cpp \
	app/platform/linux/linux_platform_common.cpp \
	app/tools/sysupgrade/upgrade_flash.cpp
SYSUPGRADE_OBJS := $(patsubst app/%.cpp,$(OBJ_DIR)/sysupgrade_%.o,$(SYSUPGRADE_SRCS))
SYSUPGRADE_DEPS := $(SYSUPGRADE_OBJS:.o=.d)
WEB_INPUTS := \
	www/index.html \
	www/package.json \
	www/package-lock.json \
	www/tsconfig.json \
	www/vite.config.ts \
	www/scripts/build.mjs \
	$(shell find www/public www/src -type f | sort)
WEB_STAMP := www/dist/.live_stream_build_stamp

define ADD_MODULE_LIBRARY
MODULE_LIBS += $(LIB_DIR)/lib$(1).a
$(LIB_DIR)/lib$(1).a:
	$(MAKE) -C libs/$(1) ROOT_DIR=$(ROOT_DIR)
endef

include $(addprefix libs/,$(addsuffix /module.mk,$(MODULES)))

.PHONY: all test test-build host-test board-test board-test-build clean \
	thirdparty compiledb debug release \
	$(MODULES)

all: debug

thirdparty: $(THIRDPARTY_LIBS)

compiledb:
	@if command -v bear >/dev/null 2>&1; then \
		bear -- $(MAKE) -j2; \
	elif command -v compiledb >/dev/null 2>&1; then \
		compiledb make -j2; \
	else \
		echo "Install bear or compiledb to generate compile_commands.json"; \
		exit 1; \
	fi

$(THIRDPARTY_LIBS): $(THIRDPARTY_SRC)/build_deps.sh
	$(THIRDPARTY_SRC)/build_deps.sh

$(MODULES):
	$(MAKE) -C libs/$@ ROOT_DIR=$(ROOT_DIR) \
	  ENABLE_HISI_MPP=1

$(OBJ_DIR)/%.o: app/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(OBJ_DIR)/sysupgrade_%.o: app/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(BIN_DIR)/live_stream: $(APP_OBJS) $(MODULES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ \
	  -Wl,--start-group \
	  $(APP_OBJS) \
	  $(MODULE_LIBS) $(LIB_DIR)/libinfra.a \
	  $(THIRDPARTY_LIBS) $(HISI_MPP_STATIC_LIBS) \
	  -Wl,--end-group \
	  $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/live_sysupgrade: $(SYSUPGRADE_OBJS) $(LIB_DIR)/libsystem.a $(LIB_DIR)/libinfra.a $(THIRDPARTY_INSTALL)/lib/libcrypto.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ \
	  $(SYSUPGRADE_OBJS) \
	  $(LIB_DIR)/libsystem.a $(LIB_DIR)/libinfra.a \
	  $(THIRDPARTY_INSTALL)/lib/libcrypto.a \
	  $(SYSUPGRADE_LDFLAGS) $(LDFLAGS) $(LDLIBS)

$(WEB_STAMP): $(WEB_INPUTS)
	cd www && npm run build
	@touch $@

debug: $(MODULES) $(BIN_DIR)/live_stream $(WEB_STAMP)
	scripts/package_debug.sh $(DEBUG_DIR)

release: $(MODULES) $(BIN_DIR)/live_stream $(BIN_DIR)/live_sysupgrade $(WEB_STAMP)
	scripts/package_release.sh $(RELEASE_DIR) $(RELEASE_VERSION) $(RELEASE_PROFILE)

test: host-test

host-test:
	python3 scripts/check_http_web_contract.py
	python3 scripts/check_cpp_style_contract.py
	cd www && npm run build

board-test:
	@for module in $(MODULES); do \
		$(MAKE) -C libs/$$module ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) ENABLE_HISI_MPP=1 \
		  board-test || exit $$?; \
	done

board-test-build:
	@for module in $(MODULES); do \
		$(MAKE) -C libs/$$module ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) ENABLE_HISI_MPP=1 \
		  board-test-build || exit $$?; \
	done

test-build: board-test-build

clean:
	@for module in $(MODULES); do \
		$(MAKE) -C libs/$$module ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) clean || exit $$?; \
	done
	rm -rf $(OBJ_DIR) $(BIN_DIR)/live_stream $(BIN_DIR)/live_sysupgrade \
	  $(DEBUG_DIR) $(RELEASE_DIR)

-include $(APP_DEPS) $(SYSUPGRADE_DEPS)
