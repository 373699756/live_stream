SERVICE_NAME := media_source
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

EXTRA_TEST_DEPS := $(INFRA_LIB) $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
EXTRA_TEST_LIBS := $(MEDIA_CODEC_LIB) $(MEDIA_LIB) $(INFRA_LIB) -pthread

include ../service_rules.mk
