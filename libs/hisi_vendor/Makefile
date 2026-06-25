MODULE_NAME := hisi_vendor
.DEFAULT_GOAL := all


ENABLE_HISI_MPP ?= 1

ifeq ($(ENABLE_HISI_MPP),1)
# include $(ROOT_DIR)/libs/hisi_vendor/toolchain_hi3516dv300.mk
include ./toolchain_hi3516dv300.mk
CXXFLAGS += $(CPU_FLAGS)
CXXFLAGS += $(HISI_DEFINES)
CXXFLAGS += -I$(HISI_MPP_INC)

CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include


include ../module_rules.mk

endif
