SERVICE_NAME := media_source
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libstream_codec.a
STREAM_MUX_LIB := $(BUILD_DIR)/lib/libstream_mux.a

CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_mux/include

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../stream_codec all

$(STREAM_MUX_LIB): $(STREAM_CODEC_LIB)
	$(MAKE) -C ../stream_mux all

EXTRA_TEST_DEPS := $(INFRA_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(STREAM_MUX_LIB)
EXTRA_TEST_LIBS := $(STREAM_MUX_LIB) $(STREAM_CODEC_LIB) $(MEDIA_LIB) \
	$(INFRA_LIB) -pthread

include ../service_rules.mk
