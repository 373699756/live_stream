MODULE_NAME := time
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra

include ../module_rules.mk
