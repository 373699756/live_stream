MODULE_NAME := media_pipeline
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
EVENT_LIB := $(BUILD_DIR)/lib/libevent.a
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/src
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
EXTRA_TEST_DEPS := $(INFRA_LIB) $(MEDIA_LIB) $(MEDIA_SOURCE_LIB) \
	$(MEDIA_CODEC_LIB) $(EVENT_LIB)
EXTRA_TEST_LIBS := $(MEDIA_SOURCE_LIB) $(MEDIA_CODEC_LIB) $(MEDIA_LIB) \
	$(EVENT_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../device_media all

$(EVENT_LIB): $(INFRA_LIB)
	$(MAKE) -C ../event all

$(MEDIA_SOURCE_LIB): $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media_source all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

include ../module_rules.mk
