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
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src/metaRTC_src/include
NET_LIB := $(BUILD_DIR)/lib/libnet.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
STREAM_MUX_LIB := $(BUILD_DIR)/lib/libmedia_mux.a
METARTC_LIBS := $(ROOT_DIR)/3rdparty/install/lib/libmetartc8.a \
	$(ROOT_DIR)/3rdparty/install/lib/libmetartccore8.a \
	$(ROOT_DIR)/3rdparty/install/lib/libyangutil8.a \
	$(ROOT_DIR)/3rdparty/install/lib/libsrtp2.a \
	$(ROOT_DIR)/3rdparty/install/lib/libusrsctp.a \
	$(ROOT_DIR)/3rdparty/install/lib/libssl.a \
	$(ROOT_DIR)/3rdparty/install/lib/libcrypto.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) \
	$(MEDIA_SOURCE_LIB) $(STREAM_CODEC_LIB) $(STREAM_MUX_LIB) \
	$(METARTC_LIBS)
EXTRA_TEST_LIBS := $(MEDIA_SOURCE_LIB) $(STREAM_MUX_LIB) \
	$(STREAM_CODEC_LIB) $(MEDIA_LIB) $(NET_LIB) \
	$(INFRA_LIB) $(METARTC_LIBS) -pthread -lm -ldl

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(MEDIA_SOURCE_LIB): $(MEDIA_LIB) $(STREAM_CODEC_LIB) $(STREAM_MUX_LIB)
	$(MAKE) -C ../media_source all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(STREAM_MUX_LIB): $(STREAM_CODEC_LIB)
	$(MAKE) -C ../media_mux all

include ../service_rules.mk
