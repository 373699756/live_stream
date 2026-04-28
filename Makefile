CXX ?= g++
AR ?= ar

BUILD_DIR ?= build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj/app
BIN_DIR := $(BUILD_DIR)/bin
THIRDPARTY_DIR := 3rdparty
METARTC_SRC := $(THIRDPARTY_DIR)/metaRTC_src
METARTC_BUILD := $(THIRDPARTY_DIR)/build
METARTC_INSTALL := $(THIRDPARTY_DIR)/install
METARTC_TOOLCHAIN_FILE := $(CURDIR)/$(THIRDPARTY_DIR)/toolchains/arm-himix200-linux.cmake

MBEDTLS_LIBS := $(METARTC_INSTALL)/lib/libmbedtls.a $(METARTC_INSTALL)/lib/libmbedx509.a $(METARTC_INSTALL)/lib/libmbedcrypto.a
SRTP_LIBS := $(METARTC_INSTALL)/lib/libsrtp2.a
USRSCTP_LIBS := $(METARTC_INSTALL)/lib/libusrsctp.a
METARTC_LIBS := $(METARTC_BUILD)/meta_libmetartccore8/libmetartccore8.a $(METARTC_BUILD)/meta_libyangutil8/libyangutil8.a
THIRDPARTY_LIBS := $(METARTC_LIBS) $(SRTP_LIBS) $(USRSCTP_LIBS) $(MBEDTLS_LIBS)

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += -Ilibs/infra_service/include
CXXFLAGS += -Ilibs/logger_service/include
CXXFLAGS += -Ilibs/config_service/include
CXXFLAGS += -Ilibs/event_service/include
CXXFLAGS += -Ilibs/auth_service/include
CXXFLAGS += -Ilibs/netframe_service/include
CXXFLAGS += -Ilibs/time_service/include
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
	media_service \
	osd_service \
	rtsp_service \
	webrtc_service \
	snapshot_service \
	onvif_service \
	alarm_service \
	upgrade_service \
	http_service

SERVICE_LIBS :=

define ADD_SERVICE_LIBRARY
SERVICE_LIBS += $(LIB_DIR)/lib$(1).a
$(LIB_DIR)/lib$(1).a:
	$(MAKE) -C libs/$(1)
endef

include $(addprefix libs/,$(addsuffix /module.mk,$(SERVICES)))

.PHONY: all test clean thirdparty $(SERVICES)

all: $(SERVICES) $(BIN_DIR)/live_stream

thirdparty: $(THIRDPARTY_LIBS)

$(METARTC_INSTALL)/lib/libmbedtls.a $(METARTC_INSTALL)/lib/libmbedx509.a $(METARTC_INSTALL)/lib/libmbedcrypto.a $(METARTC_INSTALL)/lib/libsrtp2.a $(METARTC_INSTALL)/lib/libusrsctp.a: $(THIRDPARTY_DIR)/build_deps.sh
	$(THIRDPARTY_DIR)/build_deps.sh

$(METARTC_BUILD)/meta_libyangutil8/libyangutil8.a: $(METARTC_INSTALL)/lib/libmbedtls.a $(METARTC_INSTALL)/lib/libsrtp2.a $(METARTC_INSTALL)/lib/libusrsctp.a $(METARTC_SRC)/libyangutil8/CMakeLists.txt
	rm -rf $(METARTC_BUILD)/meta_libyangutil8
	cmake -S $(METARTC_SRC)/libyangutil8 -B $(METARTC_BUILD)/meta_libyangutil8 -DCMAKE_TOOLCHAIN_FILE=$(METARTC_TOOLCHAIN_FILE) -DYang_Moc=2 -DCMAKE_BUILD_TYPE=Release
	cmake --build $(METARTC_BUILD)/meta_libyangutil8 -j4

$(METARTC_BUILD)/meta_libmetartccore8/libmetartccore8.a: $(METARTC_BUILD)/meta_libyangutil8/libyangutil8.a $(METARTC_SRC)/libmetartccore8/CMakeLists.txt
	rm -rf $(METARTC_BUILD)/meta_libmetartccore8
	cmake -S $(METARTC_SRC)/libmetartccore8 -B $(METARTC_BUILD)/meta_libmetartccore8 -DCMAKE_TOOLCHAIN_FILE=$(METARTC_TOOLCHAIN_FILE) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(METARTC_BUILD)/meta_libmetartccore8 -j4

$(SERVICES):
	$(MAKE) -C libs/$@

$(OBJ_DIR)/main.o: app/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/live_stream: $(OBJ_DIR)/main.o $(SERVICES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(SERVICE_LIBS) $(LIB_DIR)/libinfra_service.a -pthread -o $@

test:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service test || exit $$?; \
	done

clean:
	@for service in $(SERVICES); do \
		$(MAKE) -C libs/$$service clean || exit $$?; \
	done
	rm -rf $(OBJ_DIR) $(BIN_DIR)/live_stream
