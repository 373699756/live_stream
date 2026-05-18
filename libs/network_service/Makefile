SERVICE_NAME := network_service
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
