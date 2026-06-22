MODULE_NAME := http
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth.a
ALARM_LIB := $(BUILD_DIR)/lib/libalarm.a
LOGGER_LIB := $(BUILD_DIR)/lib/liblogger.a
NET_LIB := $(BUILD_DIR)/lib/libnet.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice.a
MEDIA_STREAMS_LIB := $(BUILD_DIR)/lib/libmedia.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
HTTP_MEDIA_LIB := $(BUILD_DIR)/lib/libhttp_media.a
SYSTEM_LIB := $(BUILD_DIR)/lib/libsystem.a
ONVIF_LIB := $(BUILD_DIR)/lib/libonvif.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/ai/include
CXXFLAGS += -I$(ROOT_DIR)/libs/alarm/include
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/onvif/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc/include
CXXFLAGS += -I$(ROOT_DIR)/libs/http_media/include
CXXFLAGS += -Isrc

WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc.a

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(ALARM_LIB) $(LOGGER_LIB) $(NET_LIB) \
	$(MEDIA_LIB) $(MEDIA_STREAMS_LIB) $(MEDIA_CODEC_LIB) \
	$(SYSTEM_LIB) \
	$(WEBRTC_LIB) $(HTTP_MEDIA_LIB) $(ONVIF_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(ALARM_LIB) $(LOGGER_LIB) $(NET_LIB) $(WEBRTC_LIB) \
	$(HTTP_MEDIA_LIB) $(MEDIA_STREAMS_LIB) \
	$(SYSTEM_LIB) $(ONVIF_LIB) \
	$(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth all

$(ALARM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../alarm all

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device all

$(MEDIA_STREAMS_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(SYSTEM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../system all

$(ONVIF_LIB): $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(SYSTEM_LIB)
	$(MAKE) -C ../onvif all

$(WEBRTC_LIB): $(MEDIA_LIB) $(NET_LIB)
	$(MAKE) -C ../webrtc all

$(HTTP_MEDIA_LIB): $(MEDIA_STREAMS_LIB) $(WEBRTC_LIB)
	$(MAKE) -C ../http_media all

include ../module_rules.mk

HANDLER_SRCS := $(wildcard src/handlers/*.cpp)
HANDLER_OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(HANDLER_SRCS))
$(LIB_DIR)/lib$(MODULE_NAME).a: $(HANDLER_OBJS)

$(OBJ_DIR)/handlers/%.o: src/handlers/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
