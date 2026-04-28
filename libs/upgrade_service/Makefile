SERVICE_NAME := upgrade_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) -pthread
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../logger_service/include
CXXFLAGS += -I../config_service/include
CXXFLAGS += -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
