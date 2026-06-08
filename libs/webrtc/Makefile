SERVICE_NAME := webrtc
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtp/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src/openssl-1.1.1w/include
NET_LIB := $(BUILD_DIR)/lib/libnet.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
RTP_LIB := $(BUILD_DIR)/lib/librtp.a
WEBRTC_THIRDPARTY_LIBS := $(ROOT_DIR)/3rdparty/install/lib/libsrtp2.a \
	$(ROOT_DIR)/3rdparty/install/lib/libssl.a \
	$(ROOT_DIR)/3rdparty/install/lib/libcrypto.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) \
	$(MEDIA_SOURCE_LIB) $(MEDIA_CODEC_LIB) $(RTP_LIB) \
	$(WEBRTC_THIRDPARTY_LIBS)
EXTRA_TEST_LIBS := $(MEDIA_SOURCE_LIB) $(RTP_LIB) \
	$(MEDIA_CODEC_LIB) $(MEDIA_LIB) $(NET_LIB) \
	$(INFRA_LIB) $(WEBRTC_THIRDPARTY_LIBS) -pthread -lm -ldl

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(MEDIA_SOURCE_LIB): $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media_source all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(RTP_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../rtp all

include ../service_rules.mk
