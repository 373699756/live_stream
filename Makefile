.DEFAULT_GOAL := all

ROOT_DIR := $(CURDIR)
BUILD_DIR ?= build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj/app
BIN_DIR := $(BUILD_DIR)/bin
THIRDPARTY_DIR := 3rdparty
THIRDPARTY_SRC := $(THIRDPARTY_DIR)/open_src
METARTC_SRC := $(THIRDPARTY_SRC)/metaRTC_src
METARTC_INSTALL := $(THIRDPARTY_DIR)/install
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

OPENSSL_LIBS := $(METARTC_INSTALL)/lib/libssl.a $(METARTC_INSTALL)/lib/libcrypto.a
SRTP_LIBS := $(METARTC_INSTALL)/lib/libsrtp2.a
USRSCTP_LIBS := $(METARTC_INSTALL)/lib/libusrsctp.a
METARTC_LIBS := $(METARTC_INSTALL)/lib/libmetartc8.a $(METARTC_INSTALL)/lib/libmetartccore8.a $(METARTC_INSTALL)/lib/libyangutil8.a
THIRDPARTY_LIBS := $(METARTC_LIBS) $(SRTP_LIBS) $(USRSCTP_LIBS) $(OPENSSL_LIBS)
SYSUPGRADE_LDFLAGS ?= -static

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-date-time
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += $(CPU_FLAGS)
CXXFLAGS += $(HISI_DEFINES)
CXXFLAGS += -DLIVE_STREAM_ENABLE_HISI_MPP
CXXFLAGS += -Iapp
CXXFLAGS += -Ilibs/infra_service/include
CXXFLAGS += -Ilibs/logger_service/include
CXXFLAGS += -Ilibs/config_service/include
CXXFLAGS += -Ilibs/auth_service/include
CXXFLAGS += -Ilibs/event_service/include
CXXFLAGS += -Ilibs/system_service/include
CXXFLAGS += -Ilibs/network_service/include
CXXFLAGS += -Ilibs/network_service/src
CXXFLAGS += -Ilibs/time_service/include
CXXFLAGS += -Ilibs/stream_codec/include
CXXFLAGS += -Ilibs/stream_mux/include
CXXFLAGS += -Ilibs/net_service/include
CXXFLAGS += -Ilibs/ai_service/include
CXXFLAGS += -Ilibs/media_service/include
CXXFLAGS += -Ilibs/media_source/include
CXXFLAGS += -Ilibs/media_source_service/include
CXXFLAGS += -Ilibs/hisi_vendor/include
CXXFLAGS += -Ilibs/region_service/include
CXXFLAGS += -Ilibs/rtsp_service/include
CXXFLAGS += -Ilibs/webrtc_service/include
CXXFLAGS += -Ilibs/snapshot_service/include
CXXFLAGS += -Ilibs/onvif_service/include
CXXFLAGS += -Ilibs/alarm_service/include
CXXFLAGS += -Ilibs/upgrade_service/include
CXXFLAGS += -Ilibs/http_service/include
CXXFLAGS += -I$(METARTC_INSTALL)/include
CXXFLAGS += -I$(THIRDPARTY_SRC)/openssl-1.1.1w/include
CXXFLAGS += -I$(THIRDPARTY_SRC)
CXXFLAGS += -I$(HISI_MPP_INC)
CXXFLAGS += -pthread

SERVICES := \
	infra_service \
	logger_service \
	net_service \
	config_service \
	event_service \
	auth_service \
	system_service \
	network_service \
	time_service \
	ai_service \
	hisi_vendor \
	media_service \
	media_source \
	media_source_service \
	region_service \
	rtsp_service \
	webrtc_service \
	snapshot_service \
	onvif_service \
	alarm_service \
	upgrade_service \
	http_service \
	stream_codec \
	stream_mux

SERVICE_LIBS :=
APP_SRCS := \
	app/main.cpp \
	app/app_runtime.cpp \
	app/core_services.cpp \
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
	  $(SERVICE_LIBS) $(LIB_DIR)/libinfra_service.a \
	  $(THIRDPARTY_LIBS) $(HISI_MPP_STATIC_LIBS) \
	  -Wl,--end-group \
	  $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/live_sysupgrade: $(SYSUPGRADE_OBJS) $(LIB_DIR)/libinfra_service.a $(METARTC_INSTALL)/lib/libcrypto.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ \
	  $(SYSUPGRADE_OBJS) \
	  $(LIB_DIR)/libinfra_service.a $(METARTC_INSTALL)/lib/libcrypto.a \
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
