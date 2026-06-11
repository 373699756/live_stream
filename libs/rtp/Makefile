MODULE_NAME := rtp
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

EXTRA_TEST_DEPS := $(INFRA_LIB) $(MEDIA_CODEC_LIB)
EXTRA_TEST_LIBS := $(MEDIA_CODEC_LIB) $(INFRA_LIB)

include ../module_rules.mk
