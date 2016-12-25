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

#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    printf(fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif

#define PORT 12307  // the port users will be connecting to
#define PROC_BUFF 8192
//unsigned char proc_buff[PROC_BUFF];

#define CMD_BUFF 8192
//unsigned char cmd_buff[CMD_BUFF];

struct jrpc_server my_server;

unsigned char *endstring = "lepdendstring";

cJSON * say_hello(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	return cJSON_CreateString("Hello!lepdendstring");
}

#include "sysstat.h"
#include "busybox.h"
#include "procrank.h"
#include "iotop.h"
#include "ps.h"
#include <unistd.h>  

#define LOOKUP_TABLE_COUNT 32
#define MAX_CMD_ARGV 32
#define COMMAND(name) name##_main
#define CMD_OUTPUT "./output.txt"
enum {
	CMD_TYPE_NONE,
	CMD_TYPE_SYS,
	CMD_TYPE_PROC,
	CMD_TYPE_BUILTIN,
	CMD_TYPE_PERF,
	CMD_TYPE_MAX,
};
typedef int (*builtin_func)(int argc, char **argv, int fd);
typedef struct
{
	char* name;
	int type;
	pthread_mutex_t lock;
	builtin_func func;

} builtin_func_info;

static builtin_func_info lookup_table[LOOKUP_TABLE_COUNT] = {
	{
		.name = "sys",
		.type = CMD_TYPE_SYS,
		.func = NULL,
	},
	{
		.name = "proc",
		.type = CMD_TYPE_PROC,
		.func = NULL,
	},
	{
		.name = "perf",
		.type = CMD_TYPE_PERF,
		.func = NULL,
	},
	{
		.name = "ps",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(ps),
	},
	{
		.name = "iostat",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(iostat),
	},
	{
		.name = "mpstat",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(mpstat),
	},
	{
		.name = "free",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(free),
	},
	{
		.name = "top",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(top),
	},
	{
		.name = "procrank",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(procrank),
	},
	{
		.name = "iotop",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(iotop),
	},
	{
		.name = "df",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(df),
	},
	{
		.name = "dmesg",
		.type = CMD_TYPE_BUILTIN,
		.func = COMMAND(dmesg),
	},
	{
		.name = NULL,
		.func = NULL,
	},
};

void init_built_func_table(){
	/*int i = 0;
	for( ; i < LOOKUP_TABLE_COUNT; i++){
		if(lookup_table[i].name == NULL)
			return;
		pthread_mutex_init(&lookup_table[i].lock, NULL);

	}*/
	builtin_func_info* p = lookup_table;
	while(p->name != NULL){
		pthread_mutex_init(&p->lock, NULL);
		p++;
	}
}
builtin_func_info* lookup_func(char* name){
	/*int i = 0;
	for( ; i < LOOKUP_TABLE_COUNT; i++){
		if(lookup_table[i].name == NULL)
			return NULL;
		if(!strcmp(name, lookup_table[i].name))
			return &lookup_table[i];
	}*/
	builtin_func_info* p = lookup_table;
	while(p->name != NULL){
		if(!strcmp(name, p->name))
			return p;
		p++;
	}
	return NULL;
}

cJSON * read_proc(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	int fd;
	int size;
	cJSON *result;
	unsigned char proc_buff[PROC_BUFF];
	unsigned char proc_path[50];

	if (!ctx->data)
		return NULL;

	builtin_func_info* info = lookup_func("proc");
	snprintf(proc_path, 50, "/proc/%s", ctx->data);


	pthread_mutex_lock(&info->lock);
	DEBUG_PRINT("read_proc: path: %s\n", proc_path);
	fd = open(proc_path, O_RDONLY);
	if (fd < 0) {
		printf("Open file:%s error.\n", proc_path);
		return NULL;
	}

	memset(proc_buff, 0, PROC_BUFF);
	size = read(fd, proc_buff, PROC_BUFF);
	close(fd);
	DEBUG_PRINT("read %d bytes from %s\n", size, proc_path);
	strcat(proc_buff, endstring);
	pthread_mutex_unlock(&info->lock);
	return cJSON_CreateString(proc_buff);
}
cJSON * run_builtin_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	unsigned char cmd_buff[CMD_BUFF];

        if (!ctx->data)
                return NULL;

	int argc = 0;  
   	char *argv[MAX_CMD_ARGV];
	memset(argv, 0, MAX_CMD_ARGV);

	char* p = malloc(strlen(ctx->data) + 1);
	memset(p, 0, strlen(ctx->data) + 1);
        strcpy(p, ctx->data);		
        char c[] = " ";  
        char *r = strtok(p, c);  
  	argv[argc++] = r;
        
	builtin_func_info* info = lookup_func(r);
  
        while (r != NULL) {  
                r = strtok(NULL, c);  
		
		if(r != NULL){
		   argv[argc++] = r;
		}

        }

	argv[argc] = NULL;;
	if(info->func != NULL){
		pthread_mutex_lock(&info->lock);

		DEBUG_PRINT("run_builtin_cmd:%s\n",ctx->data);
		memset(cmd_buff, 0, CMD_BUFF);
 		fflush(stdout);
		int fd[2];
   		if(pipe(fd))   {
      		    DEBUG_PRINT("pipe error!\n");
      		    return NULL;
   		}

		//int bak_fd = dup(STDOUT_FILENO);
   		//int new_fd = dup2(fd[1], STDOUT_FILENO);
                info->func(argc, argv, fd[1]);
		int size = read(fd[0], cmd_buff, CMD_BUFF- strlen(endstring) - 1);

                //dup2(bak_fd, new_fd);
		close(fd[0]);
		close(fd[1]);
		//close(bak_fd);
		//close(new_fd);
		DEBUG_PRINT("read size:%d\n", size);
		strcat(cmd_buff, endstring);
	        free(p);


	        pthread_mutex_unlock(&info->lock);
		return cJSON_CreateString(cmd_buff);

	}

	free(p);
	return NULL;
}
//#else
cJSON * run_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;
	unsigned char cmd_buff[CMD_BUFF];

	if (!ctx->data)
		return NULL;

	builtin_func_info* info = lookup_func("sys");
	fp = popen(ctx->data, "r");
	if (fp) {
		pthread_mutex_lock(&info->lock);
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF - strlen(endstring) - 1 , fp);
		DEBUG_PRINT("run_cmd:size %d:%s\n", size, ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);	
		pthread_mutex_unlock(&info->lock);
		return cJSON_CreateString(cmd_buff);
	}
	return NULL;
}
//#endif

cJSON * run_perf_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;
	unsigned char cmd_buff[CMD_BUFF];

	if (!ctx->data)
		return NULL;


	builtin_func_info* info = lookup_func("perf");
	pthread_mutex_lock(&info->lock);

	DEBUG_PRINT("run_perf_cmd\n");
	system(ctx->data);
	fp = popen("perf report", "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF - strlen(endstring) - 1, fp);
		DEBUG_PRINT("run_cmd:size %d:%s\n", size, ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);	
		pthread_mutex_unlock(&info->lock);
		return cJSON_CreateString(cmd_buff);
	}

	pthread_mutex_unlock(&info->lock);
	return NULL;
}
cJSON * list_all(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	int i;
	unsigned char proc_buff[CMD_BUFF];
	memset(proc_buff, 0, PROC_BUFF);
	for (i = 0; i < my_server.procedure_count; i++) {
		strcat(proc_buff, my_server.procedures[i].name);
		strcat(proc_buff, " ");
	}
	strcat(proc_buff, endstring);
	return cJSON_CreateString(proc_buff);
}

int main(int argc, char **argv)
{
	if ( (argc == 2) && (strcmp(argv[1], "--debug") == 0) ){
			/* Enable debugging. */
	} else
		daemon(0, 0);

	init_built_func_table();

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
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdIotop", "iotop");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdFree", "free");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdProcrank", "procrank");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdIostat", "iostat -d -x -k");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdVmstat", "vmstat");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdTop", "top -n 1 -b | head -n 50");
	jrpc_register_procedure(&my_server, run_cmd, "GetCmdTop", "ps -e -o pid,user,pri,ni,vsize,rss,s,%cpu,%mem,time,cmd --sort=-%cpu ");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdTopH", "top -n 1 -b | head -n 50");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdIotop", "iotop -n 1 -b | head -n 50");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdSmem", "smem -p -s pss -r -n 50");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdDmesg", "dmesg");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdDf", "df -h");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdMpstat", "mpstat -P ALL 1 1");

	jrpc_register_procedure(&my_server, run_perf_cmd, "GetCmdPerfFaults", "perf record -a -e faults sleep 1");
	jrpc_register_procedure(&my_server, run_perf_cmd, "GetCmdPerfCpuclock", "perf record -a -e cpu-clock sleep 1");
	jrpc_server_run(&my_server);
	jrpc_server_destroy(&my_server);
	return 0;
}
