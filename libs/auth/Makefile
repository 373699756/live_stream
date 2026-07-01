MODULE_NAME := auth
.DEFAULT_GOAL := all
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
RUNTIME_LIB := $(BUILD_DIR)/lib/libruntime.a
CXXFLAGS += -I$(ROOT_DIR)/libs/runtime/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
EXTRA_TEST_DEPS := $(RUNTIME_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(RUNTIME_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(RUNTIME_LIB):
	$(MAKE) -C ../runtime

include ../module_rules.mk
