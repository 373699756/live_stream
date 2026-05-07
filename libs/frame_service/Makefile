SERVICE_NAME := frame_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB) $(MEDIA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

include ../service_rules.mk
