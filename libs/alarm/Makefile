MODULE_NAME := alarm
.DEFAULT_GOAL := all
INFRA_LIB = $(BUILD_DIR)/lib/libinfra.a
EVENT_LIB = $(BUILD_DIR)/lib/libevent.a
CONFIG_LIB = $(BUILD_DIR)/lib/libconfig.a
CXXFLAGS += -I$(ROOT_DIR)/libs/event/include
CXXFLAGS += -I$(ROOT_DIR)/libs/logger/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config/include
EXTRA_TEST_DEPS = $(EVENT_LIB) $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS = $(EVENT_LIB) $(CONFIG_LIB) $(INFRA_LIB) -pthread

$(INFRA_LIB):
	$(MAKE) -C ../infra

$(EVENT_LIB):
	$(MAKE) -C ../event

$(CONFIG_LIB):
	$(MAKE) -C ../config

include ../module_rules.mk
