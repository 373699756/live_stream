SERVICE_NAME := device_media
.DEFAULT_GOAL := all

include ../service_rules.mk

INFRA_LIB := $(LIB_DIR)/libinfra.a
CONFIG_LIB := $(LIB_DIR)/libconfig.a
MEDIA_CODEC_LIB := $(LIB_DIR)/libmedia_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include

EXTRA_TEST_DEPS := $(CONFIG_LIB) $(MEDIA_CODEC_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(MEDIA_CODEC_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra ROOT_DIR=$(ROOT_DIR)

$(CONFIG_LIB):
	$(MAKE) -C ../config ROOT_DIR=$(ROOT_DIR)

$(MEDIA_CODEC_LIB):
	$(MAKE) -C ../media_codec ROOT_DIR=$(ROOT_DIR)
