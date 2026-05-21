SERVICE_NAME := media_service
.DEFAULT_GOAL := all

include ../service_rules.mk

INFRA_LIB := $(LIB_DIR)/libinfra_service.a
CONFIG_LIB := $(LIB_DIR)/libconfig_service.a
STREAM_CODEC_LIB := $(LIB_DIR)/libstream_codec.a

CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include

EXTRA_TEST_DEPS := $(CONFIG_LIB) $(STREAM_CODEC_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(STREAM_CODEC_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service ROOT_DIR=$(ROOT_DIR)

$(CONFIG_LIB):
	$(MAKE) -C ../config_service ROOT_DIR=$(ROOT_DIR)

$(STREAM_CODEC_LIB):
	$(MAKE) -C ../stream_codec ROOT_DIR=$(ROOT_DIR)
