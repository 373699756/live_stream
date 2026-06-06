SERVICE_NAME := auth
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra

include ../service_rules.mk
