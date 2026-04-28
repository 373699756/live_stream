SERVICE_NAME := alarm_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../logger_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
