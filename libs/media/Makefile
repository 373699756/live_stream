MODULE_NAME := media
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

EXTRA_TEST_DEPS := $(MEDIA_CODEC_LIB)
EXTRA_TEST_LIBS := $(MEDIA_CODEC_LIB)

$(MEDIA_CODEC_LIB):
	$(MAKE) -C ../media_codec ROOT_DIR=$(ROOT_DIR)

include ../module_rules.mk
