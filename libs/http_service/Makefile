SERVICE_NAME := http_service
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
AUTH_LIB := $(BUILD_DIR)/lib/libauth_service.a
LOGGER_LIB := $(BUILD_DIR)/lib/liblogger_service.a
NET_LIB := $(BUILD_DIR)/lib/libnet_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
SYSTEM_LIB := $(BUILD_DIR)/lib/libsystem_service.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/ai_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/network_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/time_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/upgrade_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_hub_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc_service/include

SNAPSHOT_LIB := $(BUILD_DIR)/lib/libsnapshot_service.a
STREAM_HUB_LIB := $(BUILD_DIR)/lib/libstream_hub_service.a
WEBRTC_LIB := $(BUILD_DIR)/lib/libwebrtc_service.a

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) \
	$(MEDIA_LIB) $(SNAPSHOT_LIB) $(SYSTEM_LIB) $(STREAM_HUB_LIB) \
	$(WEBRTC_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(NET_LIB) $(WEBRTC_LIB) \
	$(STREAM_HUB_LIB) $(SNAPSHOT_LIB) $(SYSTEM_LIB) $(MEDIA_LIB) \
	$(INFRA_LIB)

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

$(SNAPSHOT_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../snapshot_service all

$(SYSTEM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../system_service all

$(STREAM_HUB_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../stream_hub_service all

$(WEBRTC_LIB): $(MEDIA_LIB) $(NET_LIB)
	$(MAKE) -C ../webrtc_service all

include ../service_rules.mk

# http_service.cpp #include-s all handler fragments — track them so make
# recompiles whenever any handler file changes.
HANDLER_INCS := $(wildcard src/handlers/*.cpp.inc)
$(OBJ_DIR)/http_service.o: $(HANDLER_INCS)
