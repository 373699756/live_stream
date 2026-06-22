MODULE_NAME := net_stat
.DEFAULT_GOAL := all
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include

EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libevent.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/librtsp.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libwebrtc.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libdevice.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libnet.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libevent.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/librtsp.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libwebrtc.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_codec.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libdevice.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libinfra.a

include ../module_rules.mk

$(BUILD_DIR)/lib/libnet.a:
	$(MAKE) -C ../net all

$(BUILD_DIR)/lib/libevent.a:
	$(MAKE) -C ../event all

$(BUILD_DIR)/lib/librtsp.a:
	$(MAKE) -C ../rtsp all

$(BUILD_DIR)/lib/libwebrtc.a:
	$(MAKE) -C ../webrtc all

$(BUILD_DIR)/lib/libmedia.a:
	$(MAKE) -C ../media all

$(BUILD_DIR)/lib/libmedia_codec.a:
	$(MAKE) -C ../media_codec all

$(BUILD_DIR)/lib/libdevice.a:
	$(MAKE) -C ../device all

$(BUILD_DIR)/lib/libinfra.a:
	$(MAKE) -C ../infra all
