SERVICE_NAME := media_mux
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

EXTRA_TEST_DEPS := $(INFRA_LIB) $(STREAM_CODEC_LIB) \
	$(BUILD_DIR)/obj/device_media/media_buffer.o
EXTRA_TEST_LIBS := $(STREAM_CODEC_LIB) $(INFRA_LIB) \
	$(BUILD_DIR)/obj/device_media/media_buffer.o

include ../service_rules.mk
