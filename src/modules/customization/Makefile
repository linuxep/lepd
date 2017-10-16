DIR_INC = ./inc
DIR_SRC = ./src
DIR_OBJ = ./obj


SRC = $(wildcard ${DIR_SRC}/*.c)  
OBJ = $(patsubst %.c,${DIR_OBJ}/%.o,$(notdir ${SRC})) 

TARGET = libcus.a
 

#CROSS_COMPILE=arm-linux-gnueabi-
#CC=$(CROSS_COMPILE)gcc
#AR=$(CROSS_COMPILE)ar 
#LD=$(CROSS_COMPILE)ld


CFLAGS = -I$(DIR_INC) -Wall -static


 
${TARGET}:${OBJ}
	$(AR) rc $@ $^ 
	@mv $(TARGET) ../../../libs/
${DIR_OBJ}/%.o:${DIR_SRC}/%.c
	@mkdir -p $(DIR_OBJ)
	$(CC) $(CFLAGS) -c  $< -o $@

.PHONY:clean
clean:
	@rm -rf $(DIR_OBJ)
