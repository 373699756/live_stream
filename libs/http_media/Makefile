MODULE_NAME := http_media
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB := $(BUILD_DIR)/lib/libevent.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth.a
SOCKET_IO_LIB := $(BUILD_DIR)/lib/libsocket_io.a
DEVICE_MEDIA_LIB := $(BUILD_DIR)/lib/libdevice.a
MEDIA_STREAMS_LIB := $(BUILD_DIR)/lib/libmedia.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/hisi_vendor/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/http/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/socket_io/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc/include

EXTRA_TEST_DEPS := $(EVENT_LIB) $(INFRA_LIB) $(AUTH_LIB) $(SOCKET_IO_LIB) \
	$(DEVICE_MEDIA_LIB) $(MEDIA_CODEC_LIB) $(MEDIA_STREAMS_LIB) $(WEBRTC_LIB)
EXTRA_TEST_LIBS := $(EVENT_LIB) $(AUTH_LIB) $(WEBRTC_LIB) $(MEDIA_STREAMS_LIB) \
	$(DEVICE_MEDIA_LIB) $(MEDIA_CODEC_LIB) $(SOCKET_IO_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(EVENT_LIB):
	$(MAKE) -C ../event all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth all

$(SOCKET_IO_LIB): $(INFRA_LIB)
	$(MAKE) -C ../socket_io all

$(DEVICE_MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(MEDIA_STREAMS_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media all

$(WEBRTC_LIB): $(DEVICE_MEDIA_LIB) $(SOCKET_IO_LIB)
	$(MAKE) -C ../webrtc all

include ../module_rules.mk
