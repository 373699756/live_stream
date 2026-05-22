# HiSilicon Hi3516DV300 toolchain configuration
# Synced from ipc_camera/mk/toolchain_hi3516dv300.mk

# ---- SDK auto-detection ----
HISI_VENDOR_TOOLCHAIN_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT_DIR ?= $(abspath $(HISI_VENDOR_TOOLCHAIN_DIR)/../..)
PROJECT_PARENT_DIR := $(abspath $(ROOT_DIR)/..)
PROJECT_GRANDPARENT_DIR := $(abspath $(ROOT_DIR)/../..)
PROJECT_HISI_MPP_ROOT := $(ROOT_DIR)/3rdparty/hisi_mpp
PROJECT_HISI_MPP_MARKER := $(wildcard $(PROJECT_HISI_MPP_ROOT)/include/mpi_sys.h)
HISI_SDK_ROOT_CANDIDATES := \
  $(wildcard $(PROJECT_PARENT_DIR)/Hi3516*SDK*) \
  $(wildcard $(PROJECT_GRANDPARENT_DIR)/Hi3516*SDK*) \
  $(wildcard $(HOME)/hisi/Hi3516*SDK*) \
  /home/alientek/hisi/Hi3516CV500_SDK_V2.0.1.0
HISI_SDK_ROOT ?= $(firstword $(HISI_SDK_ROOT_CANDIDATES))

HISI_MPP_ROOT_CANDIDATES := \
  $(if $(PROJECT_HISI_MPP_MARKER),$(PROJECT_HISI_MPP_ROOT)) \
  $(patsubst %/cfg.mak,%,$(wildcard $(HISI_SDK_ROOT)/smp/*/mpp/cfg.mak))
HISI_MPP_ROOT ?= $(firstword $(HISI_MPP_ROOT_CANDIDATES))
ifeq ($(strip $(HISI_MPP_ROOT)),)
HISI_MPP_ROOT := $(HISI_SDK_ROOT)/smp/a7_linux/mpp
endif
HISI_MPP_INC := $(HISI_MPP_ROOT)/include
HISI_MPP_LIB := $(HISI_MPP_ROOT)/lib

-include $(HISI_MPP_ROOT)/cfg.mak

# ---- Toolchain ----
CROSS_COMPILE ?= arm-himix200-linux-
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar
STRIP := $(CROSS_COMPILE)strip

# ---- Sensor / ISP / Architecture ----
SENSOR0_TYPE ?= SONY_IMX290_MIPI_2M_30FPS_12BIT
SENSOR1_TYPE ?= $(SENSOR0_TYPE)
ISP_VERSION ?= ISP_V2
HIARCH ?= $(if $(strip $(CONFIG_HI_ARCH)),$(CONFIG_HI_ARCH),hi3516cv500)
HI_RLS_MODE ?= $(if $(strip $(CONFIG_HI_RLS_MODE)),$(CONFIG_HI_RLS_MODE),HI_RELEASE)

# ---- CPU flags ----
CPU_FLAGS := -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4

# ---- HiSilicon defines ----
HISI_DEFINES := $(if $(strip $(HIARCH)),-D$(HIARCH)) \
  -DHI_XXXX \
  $(if $(strip $(ISP_VERSION)),-D$(ISP_VERSION)) \
  $(if $(strip $(HI_RLS_MODE)),-D$(HI_RLS_MODE)) \
  -DSENSOR0_TYPE=$(SENSOR0_TYPE) -DSENSOR1_TYPE=$(SENSOR1_TYPE)

# ---- SDK validation ----
REQUIRED_HISI_HEADERS := $(HISI_MPP_INC)/mpi_isp.h $(HISI_MPP_INC)/mpi_sys.h
MISSING_HISI_HEADERS := $(filter-out $(wildcard $(REQUIRED_HISI_HEADERS)),$(REQUIRED_HISI_HEADERS))

ifeq ($(strip $(HISI_SDK_ROOT)),)
$(error Failed to detect HISI_SDK_ROOT. Set HISI_SDK_ROOT=/path/to/Hi3516*_SDK_* or HISI_MPP_ROOT=/path/to/smp/*/mpp)
endif

ifneq ($(strip $(MISSING_HISI_HEADERS)),)
$(error Missing required HISI SDK headers: $(MISSING_HISI_HEADERS). \
  Resolved HISI_SDK_ROOT=$(HISI_SDK_ROOT) HISI_MPP_ROOT=$(HISI_MPP_ROOT). \
  Override with HISI_SDK_ROOT=/path/to/Hi3516*_SDK_* or HISI_MPP_ROOT=/path/to/smp/*/mpp)
endif

ifeq ($(strip $(HIARCH)),)
$(warning CONFIG_HI_ARCH is empty; omitting architecture define from HISI_DEFINES)
endif
ifeq ($(strip $(HI_RLS_MODE)),)
$(warning CONFIG_HI_RLS_MODE is empty; omitting release-mode define from HISI_DEFINES)
endif

# ---- HiSilicon static libraries ----
HISI_MPI_LIBS := $(HISI_MPP_LIB)/libmpi.a $(HISI_MPP_LIB)/libhdmi.a
HISI_SENSOR_LIBS := $(HISI_MPP_LIB)/lib_hiae.a \
  $(HISI_MPP_LIB)/libisp.a \
  $(HISI_MPP_LIB)/lib_hidehaze.a \
  $(HISI_MPP_LIB)/lib_hidrc.a \
  $(HISI_MPP_LIB)/lib_hildci.a \
  $(HISI_MPP_LIB)/lib_hiawb.a \
  $(HISI_MPP_LIB)/libsns_imx327.a \
  $(HISI_MPP_LIB)/libsns_imx327_2l.a \
  $(HISI_MPP_LIB)/libsns_imx307.a \
  $(HISI_MPP_LIB)/libsns_imx290.a \
  $(HISI_MPP_LIB)/libsns_imx335.a \
  $(HISI_MPP_LIB)/libsns_imx458.a \
  $(HISI_MPP_LIB)/libsns_mn34220.a \
  $(HISI_MPP_LIB)/libsns_os05a.a
HISI_OPTIONAL_AI_LIBS := $(HISI_MPP_LIB)/libive.a $(HISI_MPP_LIB)/libnnie.a
HISI_MPI_AUDIO_DEPS := $(HISI_MPP_LIB)/libVoiceEngine.a \
  $(HISI_MPP_LIB)/libupvqe.a \
  $(HISI_MPP_LIB)/libdnvqe.a
HISI_SECUREC_LIB := $(HISI_MPP_LIB)/libsecurec.a

HISI_MPP_STATIC_LIBS := $(HISI_MPI_LIBS) $(HISI_SENSOR_LIBS) \
  $(HISI_MPI_AUDIO_DEPS) $(HISI_SECUREC_LIB)
ifeq ($(CONFIG_HISI_AI_LIBS),y)
HISI_MPP_STATIC_LIBS += $(HISI_OPTIONAL_AI_LIBS)
endif

# ---- Linker flags ----
LDFLAGS := -Wl,--gc-sections
LDLIBS := -lpthread -lm -ldl -lstdc++
