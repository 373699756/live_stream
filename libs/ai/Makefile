SERVICE_NAME := ai
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
MEDIA_LIB := $(BUILD_DIR)/lib/libdevice_media.a
SNAPSHOT_LIB := $(BUILD_DIR)/lib/libsnapshot.a
STREAM_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig.a
ALARM_LIB := $(BUILD_DIR)/lib/libalarm.a

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
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/alarm/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/snapshot/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
EXTRA_TEST_DEPS := $(CONFIG_LIB) $(ALARM_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(SNAPSHOT_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(CONFIG_LIB) $(ALARM_LIB) $(MEDIA_LIB) $(STREAM_CODEC_LIB) \
	$(SNAPSHOT_LIB) $(INFRA_LIB) -pthread

ifeq ($(ENABLE_HISI_MPP),1)
include $(ROOT_DIR)/libs/hisi_vendor/toolchain_hi3516dv300.mk
EXTRA_TEST_DEPS += $(HISI_MPP_STATIC_LIBS)
EXTRA_TEST_LIBS += $(HISI_MPP_STATIC_LIBS) $(LDLIBS)
endif

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(STREAM_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(CONFIG_LIB): $(INFRA_LIB)
	$(MAKE) -C ../config all

$(ALARM_LIB): $(INFRA_LIB) $(CONFIG_LIB)
	$(MAKE) -C ../alarm all

$(MEDIA_LIB): $(INFRA_LIB) $(STREAM_CODEC_LIB) $(CONFIG_LIB)
	$(MAKE) -C ../device_media all

$(SNAPSHOT_LIB): $(INFRA_LIB) $(MEDIA_LIB) $(CONFIG_LIB)
	$(MAKE) -C ../snapshot all

include ../service_rules.mk
