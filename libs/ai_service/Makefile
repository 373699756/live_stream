SERVICE_NAME := ai_service
.DEFAULT_GOAL := all

CXXFLAGS += -pthread
CXXFLAGS += -I$(ROOT_DIR)/libs/config_service/include
CXXFLAGS += -I$(ROOT_DIR)/libs/media_service/include

include ../service_rules.mk
