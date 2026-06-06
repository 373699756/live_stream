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
CXXFLAGS += -Ilibs/time/include
CXXFLAGS += -Ilibs/media_codec/include
CXXFLAGS += -Ilibs/media_mux/include
CXXFLAGS += -Ilibs/net/include
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
CXXFLAGS += -Ilibs/upgrade/include
CXXFLAGS += -Ilibs/http/include
CXXFLAGS += -Ilibs/http_media/include
CXXFLAGS += -I$(THIRDPARTY_INSTALL)/include
CXXFLAGS += -I$(THIRDPARTY_SRC)/openssl-1.1.1w/include
CXXFLAGS += -I$(THIRDPARTY_SRC)
CXXFLAGS += -I$(HISI_MPP_INC)
CXXFLAGS += -pthread

SERVICES := \
	infra \
	logger \
	net \
	config \
	event \
	auth \
	system \
	network_config \
	time \
	ai \
	hisi_vendor \
	device_media \
	media_source \
	media_pipeline \
	region \
	rtsp \
	webrtc \
	snapshot \
	onvif \
	alarm \
	upgrade \
	http_media \
	http \
	media_codec \
	media_mux

SERVICE_LIBS :=
APP_SRCS := \
	app/main.cpp \
	app/app_runtime.cpp \
	app/core_subsystem.cpp \
	app/device_subsystem.cpp \
	app/linux_network_platform.cpp \
	app/linux_platform_common.cpp \
	app/linux_system_platform.cpp \
	app/linux_time_platform.cpp \
	app/media_subsystem.cpp \
	app/platform_factory.cpp \
	app/protocol_subsystem.cpp \
	app/runtime_config.cpp \
	app/upgrade_flash.cpp \
	app/upgrade_package.cpp \
	app/upgrade_platform.cpp
APP_OBJS := $(patsubst app/%.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))
SYSUPGRADE_SRCS := \
	app/live_sysupgrade.cpp \
	app/linux_platform_common.cpp \
	app/upgrade_flash.cpp \
	app/upgrade_package.cpp
SYSUPGRADE_OBJS := $(patsubst app/%.cpp,$(OBJ_DIR)/sysupgrade_%.o,$(SYSUPGRADE_SRCS))
WEB_INPUTS := \
	www/index.html \
	www/package.json \
	www/package-lock.json \
	www/tsconfig.json \
	www/vite.config.ts \
	www/scripts/build.mjs \
	$(shell find www/public www/src -type f | sort)
WEB_STAMP := www/dist/.live_stream_build_stamp

define ADD_SERVICE_LIBRARY
SERVICE_LIBS += $(LIB_DIR)/lib$(1).a
$(LIB_DIR)/lib$(1).a:
	$(MAKE) -C libs/$(1) ROOT_DIR=$(ROOT_DIR)
endef

include $(addprefix libs/,$(addsuffix /module.mk,$(SERVICES)))

.PHONY: all test test-build clean thirdparty compiledb debug release \
	$(SERVICES)

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

$(SERVICES):
	$(MAKE) -C libs/$@ ROOT_DIR=$(ROOT_DIR) \
	  ENABLE_HISI_MPP=1

$(OBJ_DIR)/%.o: app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/sysupgrade_%.o: app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/live_stream: $(APP_OBJS) $(SERVICES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ \
	  -Wl,--start-group \
	  $(APP_OBJS) \
	  $(SERVICE_LIBS) $(LIB_DIR)/libinfra.a \
	  $(THIRDPARTY_LIBS) $(HISI_MPP_STATIC_LIBS) \
	  -Wl,--end-group \
	  $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/live_sysupgrade: $(SYSUPGRADE_OBJS) $(LIB_DIR)/libinfra.a $(THIRDPARTY_INSTALL)/lib/libcrypto.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ \
	  $(SYSUPGRADE_OBJS) \
	  $(LIB_DIR)/libinfra.a $(THIRDPARTY_INSTALL)/lib/libcrypto.a \
	  $(SYSUPGRADE_LDFLAGS) $(LDFLAGS) $(LDLIBS)

$(WEB_STAMP): $(WEB_INPUTS)
	cd www && npm run build
	@touch $@

debug: $(SERVICES) $(BIN_DIR)/live_stream $(WEB_STAMP)
	scripts/package_debug.sh $(DEBUG_DIR)

release: $(SERVICES) $(BIN_DIR)/live_stream $(BIN_DIR)/live_sysupgrade $(WEB_STAMP)
	scripts/package_release.sh $(RELEASE_DIR) $(RELEASE_VERSION) $(RELEASE_PROFILE)

test:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) test || exit $$?; \
	done

test-build:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) ENABLE_HISI_MPP=1 \
		  test-build || exit $$?; \
	done

clean:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service ROOT_DIR=$(ROOT_DIR) \
		  BUILD_DIR=$(ROOT_DIR)/$(BUILD_DIR) clean || exit $$?; \
	done
	rm -rf $(OBJ_DIR) $(BIN_DIR)/live_stream $(BIN_DIR)/live_sysupgrade \
	  $(DEBUG_DIR) $(RELEASE_DIR)
