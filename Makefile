CROSS_COMPILE ?=
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
endif

.DEFAULT_GOAL := all

BUILD_DIR ?= build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj/app
BIN_DIR := $(BUILD_DIR)/bin
THIRDPARTY_DIR := 3rdparty
THIRDPARTY_SRC := $(THIRDPARTY_DIR)/src
METARTC_SRC := $(THIRDPARTY_SRC)/metaRTC_src
METARTC_INSTALL := $(THIRDPARTY_DIR)/install

OPENSSL_LIBS := $(METARTC_INSTALL)/lib/libssl.a $(METARTC_INSTALL)/lib/libcrypto.a
SRTP_LIBS := $(METARTC_INSTALL)/lib/libsrtp2.a
USRSCTP_LIBS := $(METARTC_INSTALL)/lib/libusrsctp.a
METARTC_LIBS := $(METARTC_INSTALL)/lib/libmetartc8.a $(METARTC_INSTALL)/lib/libmetartccore8.a $(METARTC_INSTALL)/lib/libyangutil8.a
THIRDPARTY_LIBS := $(METARTC_LIBS) $(SRTP_LIBS) $(USRSCTP_LIBS) $(OPENSSL_LIBS)

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += -Iapp
CXXFLAGS += -Ilibs/common/include
CXXFLAGS += -Ilibs/infra_service/include
CXXFLAGS += -Ilibs/logger_service/include
CXXFLAGS += -Ilibs/config_service/include
CXXFLAGS += -Ilibs/event_service/include
CXXFLAGS += -Ilibs/auth_service/include
CXXFLAGS += -Ilibs/system_service/include
CXXFLAGS += -Ilibs/network_service/include
CXXFLAGS += -Ilibs/time_service/include
CXXFLAGS += -Ilibs/netframe_service/include
CXXFLAGS += -Ilibs/ai_service/include
CXXFLAGS += -Ilibs/media_service/include
CXXFLAGS += -Ilibs/osd_service/include
CXXFLAGS += -Ilibs/rtsp_service/include
CXXFLAGS += -Ilibs/webrtc_service/include
CXXFLAGS += -Ilibs/snapshot_service/include
CXXFLAGS += -Ilibs/frame_service/include
CXXFLAGS += -Ilibs/onvif_service/include
CXXFLAGS += -Ilibs/alarm_service/include
CXXFLAGS += -Ilibs/upgrade_service/include
CXXFLAGS += -Ilibs/http_service/include
CXXFLAGS += -I$(METARTC_INSTALL)/include
CXXFLAGS += -I$(THIRDPARTY_SRC)
CXXFLAGS += -pthread

SERVICES := \
	infra_service \
	logger_service \
	netframe_service \
	config_service \
	event_service \
	auth_service \
	system_service \
	network_service \
	time_service \
	ai_service \
	media_service \
	osd_service \
	rtsp_service \
	webrtc_service \
	snapshot_service \
	frame_service \
	onvif_service \
	alarm_service \
	upgrade_service \
	http_service

SERVICE_LIBS :=
APP_SRCS := \
	app/main.cpp \
	app/app_runtime.cpp \
	app/core_services.cpp \
	app/device_platforms.cpp \
	app/device_subsystem.cpp \
	app/media_subsystem.cpp \
	app/protocol_subsystem.cpp \
	app/runtime_config.cpp
APP_OBJS := $(patsubst app/%.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))

define ADD_SERVICE_LIBRARY
SERVICE_LIBS += $(LIB_DIR)/lib$(1).a
$(LIB_DIR)/lib$(1).a:
	$(MAKE) -C libs/$(1)
endef

include $(addprefix libs/,$(addsuffix /module.mk,$(SERVICES)))

.PHONY: all test clean thirdparty $(SERVICES)

all: $(SERVICES) $(BIN_DIR)/live_stream

thirdparty: $(THIRDPARTY_LIBS)

$(THIRDPARTY_LIBS): $(THIRDPARTY_DIR)/build_deps.sh
	$(THIRDPARTY_DIR)/build_deps.sh

$(SERVICES):
	$(MAKE) -C libs/$@

$(OBJ_DIR)/%.o: app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/live_stream: $(APP_OBJS) $(SERVICES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(APP_OBJS) $(SERVICE_LIBS) $(LIB_DIR)/libinfra_service.a $(THIRDPARTY_LIBS) -ldl -pthread -o $@

test:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service test || exit $$?; \
	done

clean:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service clean || exit $$?; \
	done
	rm -rf $(OBJ_DIR) $(BIN_DIR)/live_stream
