MODULE_NAME := onvif
.DEFAULT_GOAL := all
INFRA_LIB = $(BUILD_DIR)/lib/libinfra.a
CXXFLAGS += -I$(ROOT_DIR)/libs/auth/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/device_media/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_codec/include
CXXFLAGS += -I$(ROOT_DIR)/libs/net/include
CXXFLAGS += -I$(ROOT_DIR)/libs/rtsp/include
CXXFLAGS += -I$(ROOT_DIR)/libs/system/include
EXTRA_TEST_DEPS = $(INFRA_LIB)
EXTRA_TEST_LIBS = $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra

include ../module_rules.mk
