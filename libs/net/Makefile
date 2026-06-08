MODULE_NAME := net
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_DEPS += $(INFRA_LIB)
EXTRA_TEST_LIBS += $(INFRA_LIB)
include ../module_rules.mk

all: $(INFRA_LIB)

.PHONY: FORCE
$(INFRA_LIB): FORCE
	$(MAKE) -C ../infra
