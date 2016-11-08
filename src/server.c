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

#ifdef TOOLBOX_FUN
unsigned char *func_path = "../arm-toolbox/";
#else
unsigned char *func_path = "";
#endif


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

cJSON * run_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	char cmd[128];

	if (!ctx->data)
		return NULL;

	sprintf(cmd, "%s%s", func_path,(char*)ctx->data);
	printf("cmd is %s\n", cmd);

	fp = popen(cmd, "r");
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

cJSON * run_perf_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	char cmd[128];

	if (!ctx->data)
		return NULL;

	sprintf(cmd, "%s%s", func_path,(char*)ctx->data);
	printf("cmd is %s\n", cmd);
	system(cmd);


	sprintf(cmd, "%s%s", func_path,"perf report");
	printf("cmd is %s\n", cmd);
	fp = popen(cmd, "r");
	//system(ctx->data);
	//fp = popen("perf report", "r");
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
