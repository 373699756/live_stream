SERVICE_NAME := ai_service
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
SNAPSHOT_LIB := $(BUILD_DIR)/lib/libsnapshot_service.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libstream_codec.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig_service.a

ENABLE_HISI_MPP ?= 0
ifeq ($(ENABLE_HISI_MPP),1)
include $(ROOT_DIR)/libs/hisi_vendor/toolchain_hi3516dv300.mk
CXXFLAGS += $(CPU_FLAGS)
CXXFLAGS += $(HISI_DEFINES)
CXXFLAGS += -DLIVE_STREAM_ENABLE_HISI_MPP
CXXFLAGS += -I$(HISI_MPP_INC)
ifeq ($(CONFIG_HISI_AI_LIBS),y)
CXXFLAGS += -DLIVE_STREAM_ENABLE_HISI_NNIE
endif
endif

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/stream_codec/include
EXTRA_TEST_DEPS := $(CONFIG_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(SNAPSHOT_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(SNAPSHOT_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra_service all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../stream_codec all

$(CONFIG_LIB): $(INFRA_LIB)
	$(MAKE) -C ../config_service all

$(MEDIA_LIB): $(INFRA_LIB) $(STREAM_CODEC_LIB) $(CONFIG_LIB)
	$(MAKE) -C ../media_service all

$(SNAPSHOT_LIB): $(INFRA_LIB) $(MEDIA_LIB) $(CONFIG_LIB)
	$(MAKE) -C ../snapshot_service all

include ../service_rules.mk
