MODULE_NAME := http_media
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth.a
LOGGER_LIB := $(BUILD_DIR)/lib/liblogger.a
NET_LIB := $(BUILD_DIR)/lib/libnet.a
DEVICE_MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/http/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc/include

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) \
	$(DEVICE_MEDIA_LIB) $(MEDIA_CODEC_LIB) $(MEDIA_SOURCE_LIB) $(WEBRTC_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(WEBRTC_LIB) $(MEDIA_SOURCE_LIB) \
	$(DEVICE_MEDIA_LIB) $(MEDIA_CODEC_LIB) $(NET_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth all

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(DEVICE_MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(MEDIA_SOURCE_LIB): $(DEVICE_MEDIA_LIB) $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media_source all

$(WEBRTC_LIB): $(DEVICE_MEDIA_LIB) $(NET_LIB)
	$(MAKE) -C ../webrtc all

include ../module_rules.mk
