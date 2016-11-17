PROJECT_TOP_DIR=$(PWD)
PROJECT_BIN_DIR=./bin
PROJECT_SRC_DIR=./src
PROJECT_INC_DIR=./inc
PROJECT_LIB_DIR=$(PROJECT_TOP_DIR)/libs
PROJECT_OBJ_DIR=./objs
PROJECT_EV_DIR=./arm-libev
PROJECT_MODULE_DIR=./modules
PROJECT_SYSSTAT_DIR=$(PROJECT_MODULE_DIR)/sysstat-lite
PROJECT_BUSYBOX_DIR=$(PROJECT_MODULE_DIR)/busybox-lite
MKDIR := mkdir -p


ifeq ($(ARCH), x86)
CC=gcc
AR=ar
LD=ld
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR) -D_BUILTIN_FUN
LDFLAG :=
else
CROSS_COMPILE=arm-linux-gnueabi-
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR) -D_BUILTIN_FUN
LDFLAG := -L$(PROJECT_EV_DIR)
endif

export CROSS_COMPILE CC AR LD
#DEFS = -DBUILDIN_FUNC

SUBDIRS := $(PROJECT_SYSSTAT_DIR) $(PROJECT_BUSYBOX_DIR)


TARGETS = lepd

src :=$(wildcard $(PROJECT_SRC_DIR)/*.c)
dir := $(notdir $(src))
PROJECT_OBJ := $(patsubst %.c,%.o,$(dir) )

PROJECT_ALL_OBJS := $(addprefix $(PROJECT_OBJ_DIR)/, $(PROJECT_OBJ))

all:$(PROJECT_ALL_OBJS)
	$(CC) $(wildcard $(PROJECT_SRC_DIR)/*.c) $(wildcard $(PROJECT_LIB_DIR)/*.a) $(CFLAGS) -o $(PROJECT_BIN_DIR)/$(TARGETS) $(LDFLAG)

prepare:
	cd $(foreach i, $(shell echo $(SUBDIRS)), $(i)) && $(MAKE)
	#cd $(SUBDIRS) && $(MAKE)
	$(MKDIR) $(PROJECT_OBJ_DIR)
	$(MKDIR) $(PROJECT_BIN_DIR)
 
$(PROJECT_OBJ_DIR)/%.o : $(PROJECT_SRC_DIR)/%.c prepare 
	$(CC) -c $(CFLAGS) $< -o $@ 

clean:
	rm -fr $(PROJECT_OBJ_DIR)
	rm -fr $(PROJECT_BIN_DIR)
	-rm $(PROJECT_LIB_DIR)/*
	#cd $(SUBDIRS) && $(MAKE) clean
	cd $(foreach i, $(shell echo $(SUBDIRS)), $(i)) && $(MAKE) clean

