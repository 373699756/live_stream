SERVICE_NAME := http_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
AUTH_LIB := ../../build/lib/libauth_service.a
LOGGER_LIB := ../../build/lib/liblogger_service.a
NETFRAME_LIB := ../../build/lib/libnetframe_service.a
MEDIA_LIB := ../../build/lib/libmedia_service.a
SYSTEM_LIB := ../../build/lib/libsystem_service.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/ai_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/netframe_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/network_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/time_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/upgrade_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/web_media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/webrtc_service/include

SNAPSHOT_LIB := ../../build/lib/libsnapshot_service.a
WEB_MEDIA_LIB := ../../build/lib/libweb_media_service.a
WEBRTC_LIB := ../../build/lib/libwebrtc_service.a

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NETFRAME_LIB) \
	$(MEDIA_LIB) $(SNAPSHOT_LIB) $(SYSTEM_LIB) $(WEB_MEDIA_LIB) \
	$(WEBRTC_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(NETFRAME_LIB) $(WEBRTC_LIB) \
	$(WEB_MEDIA_LIB) $(SNAPSHOT_LIB) $(SYSTEM_LIB) $(MEDIA_LIB) \
	$(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth_service all

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger_service all

$(NETFRAME_LIB): $(INFRA_LIB)
	$(MAKE) -C ../netframe_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

$(SNAPSHOT_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../snapshot_service all

$(SYSTEM_LIB): $(INFRA_LIB)
	$(MAKE) -C ../system_service all

$(WEB_MEDIA_LIB): $(MEDIA_LIB)
	$(MAKE) -C ../web_media_service all

$(WEBRTC_LIB): $(MEDIA_LIB) $(NETFRAME_LIB)
	$(MAKE) -C ../webrtc_service all

include ../service_rules.mk
