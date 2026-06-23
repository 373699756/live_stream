MODULE_NAME := device
.DEFAULT_GOAL := all

include ../module_rules.mk

INFRA_LIB := $(LIB_DIR)/libinfra.a
EVENT_LIB := $(LIB_DIR)/libevent.a
MEDIA_CORE_LIB := $(LIB_DIR)/libmedia.a
MEDIA_CODEC_LIB := $(LIB_DIR)/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

EXTRA_TEST_DEPS := $(EVENT_LIB) $(MEDIA_CORE_LIB) $(MEDIA_CODEC_LIB) \
	$(INFRA_LIB)
EXTRA_TEST_LIBS := $(EVENT_LIB) $(MEDIA_CORE_LIB) $(MEDIA_CODEC_LIB) \
	$(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra ROOT_DIR=$(ROOT_DIR)

$(EVENT_LIB):
	$(MAKE) -C ../event ROOT_DIR=$(ROOT_DIR)

$(MEDIA_CORE_LIB):
	$(MAKE) -C ../media ROOT_DIR=$(ROOT_DIR)

$(MEDIA_CODEC_LIB):
	$(MAKE) -C ../media_codec ROOT_DIR=$(ROOT_DIR)
