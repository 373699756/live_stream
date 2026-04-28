SERVICE_NAME := http_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
AUTH_LIB := ../../build/lib/libauth_service.a
LOGGER_LIB := ../../build/lib/liblogger_service.a
NETFRAME_LIB := ../../build/lib/libnetframe_service.a
MEDIA_LIB := ../../build/lib/libmedia_service.a

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/netframe_service/include

EXTRA_TEST_DEPS := $(INFRA_LIB) $(AUTH_LIB) $(LOGGER_LIB) $(NETFRAME_LIB) $(MEDIA_LIB)
EXTRA_TEST_LIBS := $(AUTH_LIB) $(LOGGER_LIB) $(NETFRAME_LIB) $(MEDIA_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(AUTH_LIB): $(INFRA_LIB)
	$(MAKE) -C ../auth_service

$(LOGGER_LIB): $(INFRA_LIB)
	$(MAKE) -C ../logger_service

$(NETFRAME_LIB): $(INFRA_LIB)
	$(MAKE) -C ../netframe_service

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service

include ../service_rules.mk
