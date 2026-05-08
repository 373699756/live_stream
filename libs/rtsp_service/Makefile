SERVICE_NAME := rtsp_service
.DEFAULT_GOAL := all
CXXFLAGS += -pthread
CXXFLAGS += -I../auth_service/include
CXXFLAGS += -I../event_service/include
CXXFLAGS += -I../media_service/include
CXXFLAGS += -I../stream_mux/include
CXXFLAGS += -I../net_service/include
EXTRA_TEST_DEPS += ../../build/lib/libmedia_service.a
EXTRA_TEST_DEPS += ../../build/lib/libstream_mux.a
EXTRA_TEST_DEPS += ../../build/lib/libnet_service.a
EXTRA_TEST_DEPS += ../../build/lib/libinfra_service.a
EXTRA_TEST_LIBS += ../../build/lib/libmedia_service.a
EXTRA_TEST_LIBS += ../../build/lib/libstream_mux.a
EXTRA_TEST_LIBS += ../../build/lib/libnet_service.a
EXTRA_TEST_LIBS += ../../build/lib/libinfra_service.a
include ../service_rules.mk

../../build/lib/libmedia_service.a: ../../build/lib/libinfra_service.a
	$(MAKE) -C ../media_service all

../../build/lib/libstream_mux.a: ../../build/lib/libinfra_service.a
	$(MAKE) -C ../stream_mux all

../../build/lib/libnet_service.a:
	$(MAKE) -C ../net_service all

../../build/lib/libinfra_service.a:
	$(MAKE) -C ../infra_service all
