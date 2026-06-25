MODULE_NAME := onvif
.DEFAULT_GOAL := all
INFRA_LIB = $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB = $(BUILD_DIR)/lib/libevent.a
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/hisi_vendor/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system/include
EXTRA_TEST_DEPS = $(EVENT_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS = $(EVENT_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(EVENT_LIB):
	$(MAKE) -C ../event

include ../module_rules.mk
