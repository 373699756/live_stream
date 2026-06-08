SERVICE_NAME := http
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth.a
LOGGER_LIB := $(BUILD_DIR)/lib/liblogger.a
NET_LIB := $(BUILD_DIR)/lib/libnet.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
HTTP_MEDIA_LIB := $(BUILD_DIR)/lib/libhttp_media.a
SYSTEM_LIB := $(BUILD_DIR)/lib/libsystem.a
UPGRADE_LIB := $(BUILD_DIR)/lib/libupgrade.a
NETWORK_LIB := $(BUILD_DIR)/lib/libnetwork_config.a
TIME_LIB := $(BUILD_DIR)/lib/libtime.a
ONVIF_LIB := $(BUILD_DIR)/lib/libonvif.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/ai/include
CXXFLAGS += -I$(ROOT_DIR)/libs/alarm/include
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/network_config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/onvif/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system/include
CXXFLAGS += -I$(ROOT_DIR)/libs/time/include
CXXFLAGS += -I$(ROOT_DIR)/libs/upgrade/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc/include
CXXFLAGS += -I$(ROOT_DIR)/libs/http_media/include
CXXFLAGS += -Isrc

SNAPSHOT_LIB := $(BUILD_DIR)/lib/libsnapshot.a
WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc.a

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) \
	$(MEDIA_LIB) $(MEDIA_SOURCE_LIB) $(MEDIA_CODEC_LIB) $(SNAPSHOT_LIB) \
	$(SYSTEM_LIB) $(UPGRADE_LIB) $(NETWORK_LIB) $(TIME_LIB) \
	$(WEBRTC_LIB) $(HTTP_MEDIA_LIB) $(ONVIF_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) $(WEBRTC_LIB) \
	$(HTTP_MEDIA_LIB) $(MEDIA_SOURCE_LIB) $(SNAPSHOT_LIB) \
	$(SYSTEM_LIB) $(UPGRADE_LIB) $(NETWORK_LIB) $(TIME_LIB) $(ONVIF_LIB) \
	$(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth all

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(MEDIA_SOURCE_LIB): $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media_source all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(SNAPSHOT_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../snapshot all

$(SYSTEM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../system all

$(UPGRADE_LIB): $(INFRA_LIB)
	$(MAKE) -C ../upgrade all

$(NETWORK_LIB): $(INFRA_LIB)
	$(MAKE) -C ../network_config all

$(TIME_LIB): $(INFRA_LIB)
	$(MAKE) -C ../time all

$(ONVIF_LIB): $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(SYSTEM_LIB) $(TIME_LIB)
	$(MAKE) -C ../onvif all

$(WEBRTC_LIB): $(MEDIA_LIB) $(NET_LIB)
	$(MAKE) -C ../webrtc all

$(HTTP_MEDIA_LIB): $(MEDIA_SOURCE_LIB) $(WEBRTC_LIB)
	$(MAKE) -C ../http_media all

include ../service_rules.mk

HANDLER_SRCS := $(wildcard src/handlers/*.cpp)
HANDLER_OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(HANDLER_SRCS))
$(LIB_DIR)/lib$(SERVICE_NAME).a: $(HANDLER_OBJS)

$(OBJ_DIR)/handlers/%.o: src/handlers/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
