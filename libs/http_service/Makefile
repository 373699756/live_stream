SERVICE_NAME := http_service
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth_service.a
LOGGER_LIB := $(BUILD_DIR)/lib/liblogger_service.a
NET_LIB := $(BUILD_DIR)/lib/libnet_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
MEDIA_SOURCE_LIB := $(BUILD_DIR)/lib/libmedia_source.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libstream_codec.a
SYSTEM_LIB := $(BUILD_DIR)/lib/libsystem_service.a
UPGRADE_LIB := $(BUILD_DIR)/lib/libupgrade_service.a
NETWORK_LIB := $(BUILD_DIR)/lib/libnetwork_service.a
TIME_LIB := $(BUILD_DIR)/lib/libtime_service.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/ai_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/alarm_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_source/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/network_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/onvif_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/time_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/upgrade_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc_service/include
CXXFLAGS += -Isrc

SNAPSHOT_LIB := $(BUILD_DIR)/lib/libsnapshot_service.a
WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc_service.a

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) \
	$(MEDIA_LIB) $(MEDIA_SOURCE_LIB) $(STREAM_CODEC_LIB) $(SNAPSHOT_LIB) \
	$(SYSTEM_LIB) $(UPGRADE_LIB) $(NETWORK_LIB) $(TIME_LIB) \
	$(WEBRTC_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) $(WEBRTC_LIB) \
	$(MEDIA_SOURCE_LIB) $(SNAPSHOT_LIB) \
	$(SYSTEM_LIB) $(UPGRADE_LIB) $(NETWORK_LIB) $(TIME_LIB) \
	$(MEDIA_LIB) $(STREAM_CODEC_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth_service all

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger_service all

$(NET_LIB): $(INFRA_LIB)
	$(MAKE) -C ../net_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

$(MEDIA_SOURCE_LIB): $(MEDIA_LIB) $(STREAM_CODEC_LIB)
	$(MAKE) -C ../media_source all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../stream_codec all

$(SNAPSHOT_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../snapshot_service all

$(SYSTEM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../system_service all

$(UPGRADE_LIB): $(INFRA_LIB)
	$(MAKE) -C ../upgrade_service all

$(NETWORK_LIB): $(INFRA_LIB)
	$(MAKE) -C ../network_service all

$(TIME_LIB): $(INFRA_LIB)
	$(MAKE) -C ../time_service all

$(WEBRTC_LIB): $(MEDIA_LIB) $(NET_LIB)
	$(MAKE) -C ../webrtc_service all

include ../service_rules.mk

HANDLER_SRCS := $(wildcard src/handlers/*.cpp)
HANDLER_OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(HANDLER_SRCS))
$(LIB_DIR)/lib$(SERVICE_NAME).a: $(HANDLER_OBJS)

$(OBJ_DIR)/handlers/%.o: src/handlers/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
