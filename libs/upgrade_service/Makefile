SERVICE_NAME := upgrade_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/event_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
