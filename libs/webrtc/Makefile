MODULE_NAME := webrtc
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtp/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src/openssl-1.1.1w/include
NET_LIB := $(BUILD_DIR)/lib/libnet.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice.a
MEDIA_STREAMS_LIB := $(BUILD_DIR)/lib/libmedia.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
RTP_LIB := $(BUILD_DIR)/lib/librtp.a
WEBRTC_THIRDPARTY_LIBS := $(ROOT_DIR)/3rdparty/install/lib/libsrtp2.a \
	$(ROOT_DIR)/3rdparty/install/lib/libssl.a \
	$(ROOT_DIR)/3rdparty/install/lib/libcrypto.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) \
	$(MEDIA_STREAMS_LIB) $(MEDIA_CODEC_LIB) $(RTP_LIB) \
	$(WEBRTC_THIRDPARTY_LIBS)
EXTRA_TEST_LIBS := $(MEDIA_STREAMS_LIB) $(RTP_LIB) \
	$(MEDIA_CODEC_LIB) $(MEDIA_LIB) $(NET_LIB) \
	$(INFRA_LIB) $(WEBRTC_THIRDPARTY_LIBS) -pthread -lm -ldl

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device all

$(MEDIA_STREAMS_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(RTP_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../rtp all

include ../module_rules.mk
