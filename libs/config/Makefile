MODULE_NAME := config
.DEFAULT_GOAL := all
INFRA_LIB = $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_DEPS = $(INFRA_LIB)
EXTRA_TEST_LIBS = $(INFRA_LIB)
$(INFRA_LIB):
	$(MAKE) -C ../infra

include ../module_rules.mk
