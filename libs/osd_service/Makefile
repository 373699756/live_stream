SERVICE_NAME := osd_service
MEDIA_LIB := ../../build/lib/libmedia_service.a
INFRA_LIB := ../../build/lib/libinfra_service.a
CXXFLAGS += -I../media_service/include
EXTRA_TEST_DEPS := $(MEDIA_LIB) $(INFRA_LIB)
EXTRA_TEST_LIBS := $(MEDIA_LIB) $(INFRA_LIB)

$(MEDIA_LIB):
	$(MAKE) -C ../media_service

$(INFRA_LIB):
	$(MAKE) -C ../infra_service

include ../service_rules.mk
