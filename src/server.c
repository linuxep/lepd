/*
 * server daemon of Linux Easy Profiling
 * Copyright (c) 2016, Bob Liu <bo-liu@hotmail.com> 
 *
 * Licensed under GPLv2 or later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "jsonrpc-c.h"


#define PORT 12307  // the port users will be connecting to
#define PROC_BUFF 8192
unsigned char proc_buff[PROC_BUFF];

#define CMD_BUFF 8192
unsigned char cmd_buff[CMD_BUFF];

struct jrpc_server my_server;

unsigned char *endstring = "lepdendstring";

cJSON * say_hello(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	return cJSON_CreateString("Hello!lepdendstring");
}

cJSON * read_proc(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	int fd;
	int size;
	cJSON *result;
	unsigned char proc_path[50];

	if (!ctx->data)
		return NULL;

	snprintf(proc_path, 50, "/proc/%s", ctx->data);
	printf("read_proc: path: %s\n", proc_path);

	fd = open(proc_path, O_RDONLY);
	if (fd < 0) {
		printf("Open file:%s error.\n", proc_path);
		return NULL;
	}

	memset(proc_buff, 0, PROC_BUFF);
	size = read(fd, proc_buff, PROC_BUFF);
	close(fd);
	printf("read %d bytes from %s\n", size, proc_path);
	strcat(proc_buff, endstring);
	return cJSON_CreateString(proc_buff);
}

#ifdef _BUILTIN_FUNC

#include "sysstat.h"

#include <unistd.h>  

#define LOOKUP_TABLE_COUNT 32
#define MAX_CMD_ARGV 32
#define COMMAND(name) name##_main
#define CMD_OUTPUT "./output.txt"
 
typedef int (*builtin_func)(int argc, char **argv);
typedef struct
{
	char* name;
	builtin_func func;

} builtin_func_info;

static builtin_func_info lookup_table[LOOKUP_TABLE_COUNT] = {
	{
		.name = "iostat",
		.func = COMMAND(iostat),
	},
	{
		.name = "mpstat",
		.func = COMMAND(mpstat),
	},
	{
		.name = NULL,
		.func = NULL,
	},
};

builtin_func lookup_func(char* name){
	int i = 0;
	for( ; i < LOOKUP_TABLE_COUNT; i++){
		if(lookup_table[i].name == NULL)
			return NULL;
		if(!strcmp(name, lookup_table[i].name))
			return lookup_table[i].func;
	}
	return NULL;
}

int read_result(char* buf){

        memset(buf, 0, CMD_BUFF);
        int fd = open(CMD_OUTPUT, O_RDONLY);
        int size = 0;
        if(fd){
                size = read(fd, buf, CMD_BUFF);
		printf("run_cmd:size %d\n", size);
                close(fd);

        }

        return size;
}

cJSON * run_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	printf("run_cmd:%s\n",ctx->data);

        if (!ctx->data)
                return NULL;

	int argc = 0;  
   	char *argv[MAX_CMD_ARGV];

	char* p = malloc(strlen(ctx->data));
        strcpy(p, ctx->data);		
        char c[] = " ";  
        char *r = strtok(p, c);  
  
        printf("func: %s\n", r);
	builtin_func func = lookup_func(r);
  
        while (r != NULL) {  
                r = strtok(NULL, c);  
                printf("para:%s\n", r);  
		
		if(r != NULL){
		   argv[argc++] = r;
		}

        }

	if(func != NULL){
		remove(CMD_OUTPUT);

		int old = dup(1);
		freopen(CMD_OUTPUT, "a", stdout); setbuf(stdout, NULL);
                //freopen(CMD_OUTPUT, "a", stderr); setbuf(stderr, NULL);
                func(argc, argv);
		dup2( old, 1 );

		read_result(cmd_buff);


		strcat(cmd_buff, endstring);
		return cJSON_CreateString(cmd_buff);

	}

	free(p);
	return NULL;
}
#else
cJSON * run_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	if (!ctx->data)
		return NULL;

	fp = popen(ctx->data, "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF, fp);
		printf("run_cmd:size %d:%s\n", size, ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);
		return cJSON_CreateString(cmd_buff);
	}
	return NULL;
}
#endif

cJSON * run_perf_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	if (!ctx->data)
		return NULL;

	system(ctx->data);
	fp = popen("perf report", "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF, fp);
		printf("run_cmd:size %d:%s\n", size, ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);
		return cJSON_CreateString(cmd_buff);
	}
	return NULL;
}
cJSON * list_all(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	int i;
	memset(proc_buff, 0, PROC_BUFF);
	for (i = 0; i < my_server.procedure_count; i++) {
		strcat(proc_buff, my_server.procedures[i].name);
		strcat(proc_buff, " ");
	}
	strcat(proc_buff, endstring);
	return cJSON_CreateString(proc_buff);
}

int main(void) {
	jrpc_server_init(&my_server, PORT);
	jrpc_register_procedure(&my_server, say_hello, "SayHello", NULL);
	jrpc_register_procedure(&my_server, list_all, "ListAllMethod", NULL);
	jrpc_register_procedure(&my_server, read_proc, "GetProcMeminfo", "meminfo");
	jrpc_register_procedure(&my_server, read_proc, "GetProcLoadavg", "loadavg");
	jrpc_register_procedure(&my_server, read_proc, "GetProcVmstat", "vmstat");
	jrpc_register_procedure(&my_server, read_proc, "GetProcZoneinfo", "zoneinfo");
	jrpc_register_procedure(&my_server, read_proc, "GetProcBuddyinfo", "buddyinfo");
	jrpc_register_procedure(&my_server, read_proc, "GetProcCpuinfo", "cpuinfo");
	jrpc_register_procedure(&my_server, read_proc, "GetProcSlabinfo", "slabinfo");
	jrpc_register_procedure(&my_server, read_proc, "GetProcSwaps", "swaps");
	jrpc_register_procedure(&my_server, read_proc, "GetProcInterrupts", "interrupts");
	jrpc_register_procedure(&my_server, read_proc, "GetProcSoftirqs", "softirqs");
	jrpc_register_procedure(&my_server, read_proc, "GetProcDiskstats", "diskstats");
	jrpc_register_procedure(&my_server, read_proc, "GetProcVersion", "version");
	jrpc_register_procedure(&my_server, read_proc, "GetProcStat", "stat");
	jrpc_register_procedure(&my_server, read_proc, "GetProcModules", "modules");

	/*********************************************
	 *
	 * ****************************************/
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdFree", "free");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdProcrank", "procrank");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdIostat", "iostat -d -x -k");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdVmstat", "vmstat");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdTop", "top -n 1 -b | head -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdTop", "ps -e -o pid,user,pri,ni,vsize,rss,s,%cpu,%mem,time,cmd --sort=-%cpu | head -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdTopH", "top -H -n 1 -b | head -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdIotop", "iotop -n 1 -b | head -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdSmem", "smem -p -s pss -r -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdDmesg", "dmesg");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdDf", "df -h");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdMpstat", "mpstat -P ALL 1 1");
	jrpc_register_procedure(&my_server, run_perf_cmd, "GetCmdPerfFaults", "perf record -a -e faults sleep 1");
	jrpc_register_procedure(&my_server, run_perf_cmd, "GetCmdPerfCpuclock", "perf record -a -e cpu-clock sleep 1");
	jrpc_server_run(&my_server);
	jrpc_server_destroy(&my_server);
	return 0;
}
