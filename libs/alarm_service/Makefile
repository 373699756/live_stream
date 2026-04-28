SERVICE_NAME := alarm_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
CONFIG_LIB := ../../build/lib/libconfig_service.a
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../logger_service/include
CXXFLAGS += -I../config_service/include
EXTRA_TEST_DEPS := $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(CONFIG_LIB):
	$(MAKE) -C ../config_service

include ../service_rules.mk
