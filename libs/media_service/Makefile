SERVICE_NAME := media_service
.DEFAULT_GOAL := all
HISI_MPP_DIR ?= /home/c/linux/hisi/Hi3516CV500_SDK_V2.0.1.0/smp/a7_linux/mpp
INFRA_LIB := ../../build/lib/libinfra_service.a
CONFIG_LIB := ../../build/lib/libconfig_service.a
CXXFLAGS += -I../config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include

ifeq ($(ENABLE_HISI_MPP),1)
CXXFLAGS += -DLIVE_STREAM_ENABLE_HISI_MPP
CXXFLAGS += -I$(HISI_MPP_DIR)/include
endif

EXTRA_TEST_DEPS := $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(CONFIG_LIB):
	$(MAKE) -C ../config_service

include ../service_rules.mk
