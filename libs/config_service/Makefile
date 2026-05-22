SERVICE_NAME := config_service
.DEFAULT_GOAL := all
INFRA_LIB = $(BUILD_DIR)/lib/libinfra_service.a
EXTRA_TEST_DEPS = $(INFRA_LIB)
EXTRA_TEST_LIBS = $(INFRA_LIB)
CXXFLAGS += -I$(ROOT_DIR)/libs/auth_service/include

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
