SERVICE_NAME := logger_service
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
