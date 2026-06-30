ROOT_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ROOT_DIR := $(ROOT_DIR)
BUILD_DIR ?= $(ROOT_DIR)/build
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj/$(MODULE_NAME)
TEST_DIR := $(BUILD_DIR)/tests
RELEASE_VERSION ?= 1.0.5

CROSS_COMPILE ?= arm-himix200-linux-
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
endif

CXXFLAGS += -std=c++17
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti
CXXFLAGS += -DLIVE_STREAM_RELEASE_VERSION=\"$(RELEASE_VERSION)\"
CXXFLAGS += -Iinclude
CXXFLAGS += -I$(ROOT_DIR)/libs/infra/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/install/include
CXXFLAGS += -I$(ROOT_DIR)/3rdparty/open_src


SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TEST_SRCS := $(wildcard tests/*.cpp)
TEST_BINS := $(patsubst tests/%.cpp,$(TEST_DIR)/$(MODULE_NAME)_%,$(TEST_SRCS))
TEST_DEPS := $(TEST_BINS:=.d)
TEST_CXXFLAGS ?= -I$(ROOT_DIR)/tests/support
EXTRA_TEST_DEPS ?=
EXTRA_TEST_LIBS ?=

.PHONY: all test test-build host-test board-test board-test-build clean

all: $(LIB_DIR)/lib$(MODULE_NAME).a

$(LIB_DIR)/lib$(MODULE_NAME).a: $(OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: src/%.cpp Makefile $(ROOT_DIR)/libs/module_rules.mk
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(TEST_DIR)/$(MODULE_NAME)_%: tests/%.cpp $(LIB_DIR)/lib$(MODULE_NAME).a $(EXTRA_TEST_DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(TEST_CXXFLAGS) -MMD -MP -MF $@.d -MT $@ \
	  $< $(LIB_DIR)/lib$(MODULE_NAME).a $(EXTRA_TEST_LIBS) -o $@

test: board-test

host-test:
	@echo "No host tests for $(MODULE_NAME); use board-test-build or board-test for cross-built tests."

board-test: all $(TEST_BINS)
	@for test_bin in $(TEST_BINS); do \
		$$test_bin || exit $$?; \
	done

board-test-build: all $(TEST_BINS)

test-build: board-test-build

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)/lib$(MODULE_NAME).a $(TEST_BINS) $(TEST_DEPS)

-include $(DEPS) $(TEST_DEPS)
