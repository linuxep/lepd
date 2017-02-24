PROJECT_TOP_DIR=$(PWD)
PROJECT_OBJ_DIR=$(PROJECT_TOP_DIR)/.objs
PROJECT_LIB_DIR=$(PROJECT_TOP_DIR)/libs
PROJECT_EV_DIR=$(PROJECT_LIB_DIR)/arm-libev
PROJECT_PREBUILT_DIR=$(PROJECT_TOP_DIR)/prebuilt-binaries

PROJECT_INC_DIR=$(PROJECT_TOP_DIR)/include
PROJECT_SRC_DIR=$(PROJECT_TOP_DIR)/src
PROJECT_MODULE_DIR=$(PROJECT_SRC_DIR)/modules
PROJECT_SYSSTAT_DIR=$(PROJECT_MODULE_DIR)/sysstat-lite
PROJECT_BUSYBOX_DIR=$(PROJECT_MODULE_DIR)/busybox-lite
PROJECT_PROCRANK_DIR=$(PROJECT_MODULE_DIR)/procrank
#PROJECT_IOPP_DIR=$(PROJECT_MODULE_DIR)/iopp
PROJECT_PS_DIR=$(PROJECT_MODULE_DIR)/ps
PROJECT_IOTOP_DIR=$(PROJECT_MODULE_DIR)/iotop
MKDIR := mkdir -p

ARCH ?= x86
ifeq ($(ARCH), x86)
CC=gcc
AR=ar
LD=ld
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR) -D_BUILTIN_FUNC
LDFLAG :=
else
CROSS_COMPILE=arm-linux-gnueabi-
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR) -D_BUILTIN_FUNC
LDFLAG := -L$(PROJECT_EV_DIR)
endif

export CROSS_COMPILE CC AR LD
#DEFS = -DBUILDIN_FUNC

SUBDIRS := $(PROJECT_SYSSTAT_DIR) \
	   $(PROJECT_BUSYBOX_DIR) \
	   $(PROJECT_PROCRANK_DIR) \
	   $(PROJECT_IOTOP_DIR) \
	   $(PROJECT_PS_DIR)


TARGETS = lepd

src :=$(wildcard $(PROJECT_SRC_DIR)/*.c)
dir := $(notdir $(src))
PROJECT_OBJ := $(patsubst %.c,%.o,$(dir) )

PROJECT_ALL_OBJS := $(addprefix $(PROJECT_OBJ_DIR)/, $(PROJECT_OBJ))

define build_libs
          for lib in $(SUBDIRS) ; do \
               echo $${lib} && cd $${lib} && $(MAKE); \
          done ;
endef

define clean_libs
          for lib in $(SUBDIRS) ; do \
               echo $${lib} && cd $${lib} && $(MAKE) clean; \
          done ;
endef

all:$(PROJECT_ALL_OBJS)
	$(CC) $(wildcard $(PROJECT_SRC_DIR)/*.c) $(wildcard $(PROJECT_LIB_DIR)/*.a) $(CFLAGS) -o $(TARGETS) $(LDFLAG)

prepare:
	$(MKDIR) $(PROJECT_OBJ_DIR)
	$(MKDIR) $(PROJECT_LIB_DIR)
	$(call build_libs)
 
$(PROJECT_OBJ_DIR)/%.o : $(PROJECT_SRC_DIR)/%.c prepare 
	$(CC) -c $(CFLAGS) $< -o $@ 

clean:
	rm -fr $(PROJECT_OBJ_DIR)
	rm -fr $(TARGETS)
	-rm $(PROJECT_LIB_DIR)/*
	$(call clean_libs)

install:
ifeq ($(ARCH), x86)
	cp $(TARGETS) $(PROJECT_PREBUILT_DIR)/x86_lepd
else
	cp $(TARGETS) $(PROJECT_PREBUILT_DIR)/arm_lepd
endif
