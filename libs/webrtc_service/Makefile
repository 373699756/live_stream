SERVICE_NAME := webrtc_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_hub_service/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/src/metaRTC_src/include
NET_LIB := $(BUILD_DIR)/lib/libnet_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libstream_codec.a
STREAM_MUX_LIB := $(BUILD_DIR)/lib/libstream_mux.a
STREAM_HUB_LIB := $(BUILD_DIR)/lib/libstream_hub_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(NET_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(STREAM_MUX_LIB) $(STREAM_HUB_LIB)
EXTRA_TEST_LIBS := $(STREAM_HUB_LIB) $(STREAM_MUX_LIB) $(STREAM_CODEC_LIB) \
	$(MEDIA_LIB) $(NET_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../stream_codec all

$(STREAM_MUX_LIB): $(STREAM_CODEC_LIB)
	$(MAKE) -C ../stream_mux all

$(STREAM_HUB_LIB): $(MEDIA_LIB) $(STREAM_CODEC_LIB) $(STREAM_MUX_LIB)
	$(MAKE) -C ../stream_hub_service all

include ../service_rules.mk
