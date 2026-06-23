MODULE_NAME := ai
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB := $(BUILD_DIR)/lib/libevent.a
DEVICE_LIB := $(BUILD_DIR)/lib/libdevice.a
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia.a
MEDIA_CODEC_LIB := $(BUILD_DIR)/lib/libmedia_codec.a

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
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
EXTRA_TEST_DEPS := $(EVENT_LIB) $(DEVICE_LIB) \
	$(MEDIA_LIB) $(MEDIA_CODEC_LIB) \
	$(INFRA_LIB)
EXTRA_TEST_LIBS := $(EVENT_LIB) $(DEVICE_LIB) \
	$(MEDIA_LIB) $(MEDIA_CODEC_LIB) \
	$(INFRA_LIB) -pthread

ifeq ($(ENABLE_HISI_MPP),1)
include $(ROOT_DIR)/libs/hisi_vendor/toolchain_hi3516dv300.mk
EXTRA_TEST_DEPS += $(HISI_MPP_STATIC_LIBS)
EXTRA_TEST_LIBS += $(HISI_MPP_STATIC_LIBS) $(LDLIBS)
endif

$(INFRA_LIB):
	$(MAKE) -C ../infra all

$(EVENT_LIB):
	$(MAKE) -C ../event all

$(MEDIA_CODEC_LIB): $(INFRA_LIB)
	$(MAKE) -C ../media_codec all

$(DEVICE_LIB): $(INFRA_LIB) $(EVENT_LIB) $(MEDIA_CODEC_LIB) $(MEDIA_LIB)
	$(MAKE) -C ../device all

$(MEDIA_LIB): $(INFRA_LIB) $(MEDIA_CODEC_LIB)
	$(MAKE) -C ../media all

include ../module_rules.mk
