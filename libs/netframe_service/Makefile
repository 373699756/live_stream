SERVICE_NAME := netframe_service
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
INFRA_LIB := ../../build/lib/libinfra_service.a
EXTRA_TEST_DEPS += $(INFRA_LIB)
EXTRA_TEST_LIBS += $(INFRA_LIB)
include ../service_rules.mk

all: $(INFRA_LIB)

.PHONY: FORCE
$(INFRA_LIB): FORCE
	$(MAKE) -C ../infra_service
