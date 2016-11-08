PROJECT_TOP_DIR=$(PWD)
PROJECT_BIN_DIR=./bin
PROJECT_SRC_DIR=./src
PROJECT_INC_DIR=./inc
PROJECT_LIB_DIR=./arm-libev
PROJECT_OBJ_DIR=./objs
MKDIR := mkdir -p

ifeq ($(ARCH), x86)
CC := gcc
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR)
LDFLAG :=
DEFS = -DSYSTEM_FUNC
else
CC :=arm-linux-gnueabi-gcc
CFLAGS := -lev -lm -lrt -static -I$(PROJECT_INC_DIR)
LDFLAG := -L$(PROJECT_LIB_DIR)
DEFS = -DTOOLBOX_FUNC
endif

#DEFS = -DBUILDIN_FUNC

TARGETS = lepd

src :=$(wildcard $(PROJECT_SRC_DIR)/*.c)
dir := $(notdir $(src))
PROJECT_OBJ := $(patsubst %.c,%.o,$(dir) )

PROJECT_ALL_OBJS := $(addprefix $(PROJECT_OBJ_DIR)/, $(PROJECT_OBJ))

all:$(PROJECT_ALL_OBJS)
	$(CC) $(wildcard $(PROJECT_SRC_DIR)/*.c) $(CFLAGS) -o $(PROJECT_BIN_DIR)/$(TARGETS) $(LDFLAG)

$(PROJECT_OBJ_DIR)/%.o : $(PROJECT_SRC_DIR)/%.c  
	$(MKDIR) $(PROJECT_OBJ_DIR)  
	$(MKDIR) $(PROJECT_BIN_DIR)  
	$(CC) -c $(CFLAGS) $< -o $@ 

clean:
	rm -fr $(PROJECT_OBJ_DIR)
	rm -fr $(PROJECT_BIN_DIR)
