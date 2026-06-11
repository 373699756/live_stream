MODULE_NAME := snapshot
.DEFAULT_GOAL := all
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig.a
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
EXTRA_TEST_DEPS := $(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(CONFIG_LIB) \
	$(INFRA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(MEDIA_CODEC_LIB) $(CONFIG_LIB) \
	$(INFRA_LIB)

$(MEDIA_LIB):
	$(MAKE) -C ../device_media

$(MEDIA_CODEC_LIB):
	$(MAKE) -C ../media_codec

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(CONFIG_LIB):
	$(MAKE) -C ../config

include ../module_rules.mk
