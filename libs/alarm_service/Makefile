SERVICE_NAME := alarm_service
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig_service.a
CXXFLAGS += -I$(ROOT_DIR)/libs/event_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
EXTRA_TEST_DEPS := $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(CONFIG_LIB):
	$(MAKE) -C ../config_service

include ../service_rules.mk
