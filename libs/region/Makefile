SERVICE_NAME := region
.DEFAULT_GOAL := all
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig.a
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
EXTRA_TEST_DEPS := $(MEDIA_LIB) $(STREAM_CODEC_LIB) $(CONFIG_LIB) \
	$(INFRA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(STREAM_CODEC_LIB) $(CONFIG_LIB) \
	$(INFRA_LIB) -pthread

$(MEDIA_LIB):
	$(MAKE) -C ../device_media

$(STREAM_CODEC_LIB):
	$(MAKE) -C ../media_codec

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(CONFIG_LIB):
	$(MAKE) -C ../config

include ../service_rules.mk
