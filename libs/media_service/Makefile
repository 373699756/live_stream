SERVICE_NAME := media_service
.DEFAULT_GOAL := all

include ../service_rules.mk

INFRA_LIB := $(LIB_DIR)/libinfra_service.a
CONFIG_LIB := $(LIB_DIR)/libconfig_service.a

CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include

EXTRA_TEST_DEPS := $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service ROOT_DIR=$(ROOT_DIR)

$(CONFIG_LIB):
	$(MAKE) -C ../config_service ROOT_DIR=$(ROOT_DIR)
