SERVICE_NAME := osd_service
.DEFAULT_GOAL := all
MEDIA_LIB := ../../build/lib/libmedia_service.a
INFRA_LIB := ../../build/lib/libinfra_service.a
CONFIG_LIB := ../../build/lib/libconfig_service.a
CXXFLAGS += -I../media_service/include
CXXFLAGS += -I../config_service/include
EXTRA_TEST_DEPS := $(MEDIA_LIB) $(CONFIG_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(CONFIG_LIB) $(INFRA_LIB)

$(MEDIA_LIB):
	$(MAKE) -C ../media_service

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

$(CONFIG_LIB):
	$(MAKE) -C ../config_service

include ../service_rules.mk
