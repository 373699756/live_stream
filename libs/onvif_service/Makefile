SERVICE_NAME := onvif_service
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/time_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
