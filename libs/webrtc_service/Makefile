SERVICE_NAME := webrtc_service
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/netframe_service/include
NETFRAME_LIB := $(BUILD_DIR)/lib/libnetframe_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(NETFRAME_LIB) $(MEDIA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(NETFRAME_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(NETFRAME_LIB): $(INFRA_LIB)
	$(MAKE) -C ../netframe_service all

$(MEDIA_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_service all

include ../service_rules.mk
