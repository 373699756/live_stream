SERVICE_NAME := onvif_service
.DEFAULT_GOAL := all
INFRA_LIB := ../../build/lib/libinfra_service.a
CXXFLAGS += -I../auth_service/include
CXXFLAGS += -I../config_service/include
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../media_service/include
CXXFLAGS += -I../netframe_service/include
CXXFLAGS += -I../system_service/include
CXXFLAGS += -I../time_service/include
EXTRA_TEST_DEPS := $(INFRA_LIB)
EXTRA_TEST_LIBS := $(INFRA_LIB)

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
