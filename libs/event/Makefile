SERVICE_NAME := event
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) -pthread

include ../service_rules.mk

.PHONY: infra

$(INFRA_LIB): infra

infra:
	$(MAKE) -C ../infra
