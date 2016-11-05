arm-linux-gnueabi-gcc server.c cJSON.c jsonrpc-c.c ./modules/*.a -lev -lm -lrt -static -o lepd -I./arm-libev -L./arm-libev/ 
