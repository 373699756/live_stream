SERVICE_NAME := upgrade
.DEFAULT_GOAL := all
ROOT_DIR := ../..
BUILD_DIR ?= $(ROOT_DIR)/build
INFRA_LIB := $(BUILD_DIR)/lib/libinfra.a
CRYPTO_LIB := $(ROOT_DIR)/3rdparty/install/lib/libcrypto.a
EXTRA_TEST_DEPS := $(INFRA_LIB) $(CRYPTO_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB) $(CRYPTO_LIB) -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src/openssl-1.1.1w/include
CXXFLAGS += -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra

include ../service_rules.mk
