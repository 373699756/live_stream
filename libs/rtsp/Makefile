MODULE_NAME := rtsp
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libdevice_media.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_source.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/librtp.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_source.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libdevice_media.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/librtp.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libinfra.a
include ../module_rules.mk

$(BUILD_DIR)/lib/libdevice_media.a: $(BUILD_DIR)/lib/libinfra.a
	$(MAKE) -C ../device_media all

$(BUILD_DIR)/lib/libmedia_source.a: $(BUILD_DIR)/lib/libdevice_media.a $(BUILD_DIR)/lib/libmedia_codec.a
	$(MAKE) -C ../media_source all

$(BUILD_DIR)/lib/libmedia_codec.a: $(BUILD_DIR)/lib/libinfra.a
	$(MAKE) -C ../media_codec all

$(BUILD_DIR)/lib/librtp.a: $(BUILD_DIR)/lib/libmedia_codec.a
	$(MAKE) -C ../rtp all

$(BUILD_DIR)/lib/libnet.a:
	$(MAKE) -C ../net all

$(BUILD_DIR)/lib/libinfra.a:
	$(MAKE) -C ../infra all
