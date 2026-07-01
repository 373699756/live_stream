MODULE_NAME := rtsp
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/hisi_vendor/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/runtime/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libruntime.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libdevice.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libevent.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libruntime.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libdevice.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libevent.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libinfra.a
include ../module_rules.mk

$(BUILD_DIR)/lib/libdevice.a: $(BUILD_DIR)/lib/libinfra.a
	$(MAKE) -C ../device all

$(BUILD_DIR)/lib/libruntime.a: $(BUILD_DIR)/lib/libinfra.a
	$(MAKE) -C ../runtime all

$(BUILD_DIR)/lib/libmedia.a: $(BUILD_DIR)/lib/libmedia_codec.a
	$(MAKE) -C ../media all

$(BUILD_DIR)/lib/libmedia_codec.a: $(BUILD_DIR)/lib/libinfra.a
	$(MAKE) -C ../media_codec all

$(BUILD_DIR)/lib/libnet.a:
	$(MAKE) -C ../net all

$(BUILD_DIR)/lib/libevent.a:
	$(MAKE) -C ../event all

$(BUILD_DIR)/lib/libinfra.a:
	$(MAKE) -C ../infra all
