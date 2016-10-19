rm ./lepd
gcc server.c cJSON.c jsonrpc-c.c -lev -lm -static -o lepd
#export JRPC_DEBUG=9;./lepd
