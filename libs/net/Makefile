MODULE_NAME := net
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
CXXFLAGS += -pthread
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB := $(BUILD_DIR)/lib/libevent.a
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_DEPS += $(EVENT_LIB) $(INFRA_LIB) $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
EXTRA_TEST_LIBS += $(EVENT_LIB) $(INFRA_LIB) $(MEDIA_LIB) $(MEDIA_CODEC_LIB)
include ../module_rules.mk

all: $(EVENT_LIB) $(INFRA_LIB) $(MEDIA_LIB)

.PHONY: FORCE
$(INFRA_LIB): FORCE
	$(MAKE) -C ../infra

$(EVENT_LIB): FORCE
	$(MAKE) -C ../event

$(MEDIA_LIB): $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media

$(MEDIA_CODEC_LIB): FORCE
	$(MAKE) -C ../media_codec
