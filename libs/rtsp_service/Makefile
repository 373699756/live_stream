SERVICE_NAME := rtsp_service
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_mux/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net_service/include
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_service.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libmedia_source.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libstream_codec.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libstream_mux.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libnet_service.a
EXTRA_TEST_DEPS += $(BUILD_DIR)/lib/libinfra_service.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_source.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libmedia_service.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libstream_mux.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libstream_codec.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libnet_service.a
EXTRA_TEST_LIBS += $(BUILD_DIR)/lib/libinfra_service.a
include ../service_rules.mk

$(BUILD_DIR)/lib/libmedia_service.a: $(BUILD_DIR)/lib/libinfra_service.a
	$(MAKE) -C ../media_service all

$(BUILD_DIR)/lib/libmedia_source.a: $(BUILD_DIR)/lib/libmedia_service.a $(BUILD_DIR)/lib/libstream_codec.a $(BUILD_DIR)/lib/libstream_mux.a
	$(MAKE) -C ../media_source all

$(BUILD_DIR)/lib/libstream_codec.a: $(BUILD_DIR)/lib/libinfra_service.a
	$(MAKE) -C ../stream_codec all

$(BUILD_DIR)/lib/libstream_mux.a: $(BUILD_DIR)/lib/libstream_codec.a
	$(MAKE) -C ../stream_mux all

$(BUILD_DIR)/lib/libnet_service.a:
	$(MAKE) -C ../net_service all

$(BUILD_DIR)/lib/libinfra_service.a:
	$(MAKE) -C ../infra_service all
