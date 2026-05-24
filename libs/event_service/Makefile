SERVICE_NAME := event_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) -pthread

include ../service_rules.mk

.PHONY: infra_service

$(INFRA_LIB): infra_service

infra_service:
	$(MAKE) -C ../infra_service
