MODULE_NAME := system
.DEFAULT_GOAL := all
ROOT_DIR ?= ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB := $(BUILD_DIR)/lib/libevent.a
CRYPTO_LIB := $(ROOT_DIR)/3rdparty/install/lib/libcrypto.a
CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src/openssl-1.1.1w/include
EXTRA_TEST_DEPS := $(EVENT_LIB) $(INFRA_LIB) $(CRYPTO_LIB)
EXTRA_TEST_LIBS := $(EVENT_LIB) $(INFRA_LIB) $(CRYPTO_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(EVENT_LIB):
	$(MAKE) -C ../event

include ../module_rules.mk
