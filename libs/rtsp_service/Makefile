SERVICE_NAME := rtsp_service
CXXFLAGS += -I../auth_service/include
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../netframe_service/include
EXTRA_TEST_DEPS += ../../build/lib/libnetframe_service.a
EXTRA_TEST_DEPS += ../../build/lib/libinfra_service.a
EXTRA_TEST_LIBS += ../../build/lib/libnetframe_service.a
EXTRA_TEST_LIBS += ../../build/lib/libinfra_service.a
include ../service_rules.mk

../../build/lib/libnetframe_service.a:
	$(MAKE) -C ../netframe_service

../../build/lib/libinfra_service.a:
	$(MAKE) -C ../infra_service
