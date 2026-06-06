SERVICE_NAME := alarm
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig.a
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
EXTRA_TEST_DEPS := $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(CONFIG_LIB):
	$(MAKE) -C ../config

include ../service_rules.mk
