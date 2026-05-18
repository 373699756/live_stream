SERVICE_NAME := snapshot_service
.DEFAULT_GOAL := all
MEDIA_LIB := $(BUILD_DIR)/lib/libmedia_service.a
INFRA_LIB := $(BUILD_DIR)/lib/libinfra_service.a
CONFIG_LIB := $(BUILD_DIR)/lib/libconfig_service.a
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
EXTRA_TEST_DEPS := $(MEDIA_LIB) $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(CONFIG_LIB) $(INFRA_LIB)

$(MEDIA_LIB):
	$(MAKE) -C ../media_service

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(CONFIG_LIB):
	$(MAKE) -C ../config_service

include ../service_rules.mk
