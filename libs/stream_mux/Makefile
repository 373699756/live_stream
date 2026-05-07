SERVICE_NAME := stream_mux
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libstream_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../stream_codec all

EXTRA_TEST_DEPS := $(INFRA_LIB) $(STREAM_CODEC_LIB)
EXTRA_TEST_LIBS := $(STREAM_CODEC_LIB) $(INFRA_LIB)

include ../service_rules.mk
