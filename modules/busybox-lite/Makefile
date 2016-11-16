CROSS_COMPILE=arm-linux-gnueabi-
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar 
LD=$(CROSS_COMPILE)ld
export CROSS_COMPILE CC AR LD

CFLAGS += -I$(PWD)/include/ -I$(PWD)/ -Wall  -O2
LDFLAGS += -static
SHARELIB = 
export CFLAGS LDFLAGS


TARGET =libbusybox.a
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

all: $(OBJS)
	@ $(AR) -rc $(TARGET) $(OBJS)
	cp $(TARGET) ../../modules
clean:
	@ rm -f *.o *.a
