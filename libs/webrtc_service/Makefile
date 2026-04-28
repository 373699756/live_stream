SERVICE_NAME := webrtc_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
