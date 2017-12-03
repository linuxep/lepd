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

static int debug; /* enable this to printf */
#define DEBUG_PRINT(fmt, args...) \
	do { if(debug) \
	printf(fmt, ## args); \
	} while(0)

#define PORT 12307  // the port users will be connecting to
#define PROC_BUFF 8192
unsigned char proc_buff[PROC_BUFF];

#define CMD_BUFF 16384
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

	snprintf(proc_path, 50, "/proc/%s", (char *)ctx->data);
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
	return cJSON_CreateString(proc_buff);
}

//#ifdef _BUILTIN_FUNC

#include "sysstat.h"
#include "busybox.h"
#include "procrank.h"
#include "iotop.h"
#include "ps.h"
#include "customization.h"
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
		.name = "ps",
		.func = COMMAND(ps),
	},
	{
		.name = "iostat",
		.func = COMMAND(iostat),
	},
	{
		.name = "cpuinfo",
		.func = COMMAND(cpuinfo),
	},
	{
		.name = "mpstat",
		.func = COMMAND(mpstat),
	},
	{
		.name = "free",
		.func = COMMAND(free),
	},
	{
		.name = "top",
		.func = COMMAND(top),
	},
	{
		.name = "procrank",
		.func = COMMAND(procrank),
	},
	{
		.name = "iotop",
		.func = COMMAND(iotop),
	},
	{
		.name = "df",
		.func = COMMAND(df),
	},
	{
		.name = "dmesg",
		.func = COMMAND(dmesg),
	},
	{
		.name = "irq_info",
		.func = COMMAND(irq_info),
	},
	{
		.name = "cgtop",
		.func = COMMAND(cgtop),
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


cJSON * run_builtin_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	DEBUG_PRINT("run_builtin_cmd:%s\n",(char *)ctx->data);

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
        
	DEBUG_PRINT("func: %s\n", r);
	builtin_func func = lookup_func(r);
  
        while (r != NULL) {  
                r = strtok(NULL, c);  
		
		if(r != NULL){
		   argv[argc++] = r;
		}

        }

	argv[argc] = NULL;;
	if(func != NULL){

		memset(cmd_buff, 0, CMD_BUFF);
 		fflush(stdout);
		int fd[2];
   		if(pipe(fd))   {
      		    DEBUG_PRINT("pipe error!\n");
      		    return NULL;
   		}

		int bak_fd = dup(STDOUT_FILENO);
   		int new_fd = dup2(fd[1], STDOUT_FILENO);
                func(argc, argv);
		int size = read(fd[0], cmd_buff, CMD_BUFF- strlen(endstring) - 1);

                dup2(bak_fd, new_fd);
		close(fd[0]);
		close(fd[1]);
		close(bak_fd);
		//close(new_fd);
		DEBUG_PRINT("read size:%d\n", size);
		strcat(cmd_buff, endstring);
	        free(p);
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

	if (!ctx->data)
		return NULL;

	fp = popen(ctx->data, "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF - strlen(endstring) - 1 , fp);
		DEBUG_PRINT("run_cmd:size %d:%s\n", size, (char *)ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);
		return cJSON_CreateString(cmd_buff);
	}
	return NULL;
}
//#endif

cJSON * run_perf_report_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	if (!ctx->data)
		return NULL;
	DEBUG_PRINT("run_perf_cmd\n");
	system(ctx->data);
	fp = popen("perf report", "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF - strlen(endstring) - 1, fp);
		DEBUG_PRINT("run_cmd:size %d:%s\n", size, (char *)ctx->data);
		pclose(fp);

		strcat(cmd_buff, endstring);
		return cJSON_CreateString(cmd_buff);
	}
	return NULL;
}

cJSON * run_perf_script_cmd(jrpc_context * ctx, cJSON * params, cJSON *id)
{
	FILE *fp;
	int size;

	if (!ctx->data)
		return NULL;
	DEBUG_PRINT("run_perf_cmd\n");
	system(ctx->data);
	fp = popen("perf script", "r");
	if (fp) {
		memset(cmd_buff, 0, CMD_BUFF);
		size = fread(cmd_buff, 1, CMD_BUFF - strlen(endstring) - 1, fp);
		DEBUG_PRINT("run_cmd:size %d:%s\n", size, (char *)ctx->data);
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

int main(int argc, char **argv)
{
	int fd;

	debug = (argc == 2) && (!strcmp(argv[1], "--debug"));
	/*
	 * we need to dup2 stdout to pipes for sub-commands
	 * so, don't close them; but we want to mute errors
	 * just like a typical daemon
	 */
	daemon(0, 1);
	fd = open ("/dev/null", O_RDWR, 0);
	if (fd != -1)
		dup2 (fd, STDERR_FILENO);

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
	//jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdIopp", "iopp");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdFree", "free");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdProcrank", "procrank");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdIostat", "iostat -d -x -k");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdVmstat", "vmstat");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdTop", "top -n 1 -b | head -n 50");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdTop", "ps -e -o pid,user,pri,ni,vsize,rss,s,%cpu,%mem,time,cmd --sort=-%cpu ");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdTopH", "top -n 1 -b | head -n 50");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdIotop", "iotop -n 1 -b | head -n 50");
	//jrpc_register_procedure(&my_server, run_cmd, "GetCmdSmem", "smem -p -s pss -r -n 50");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdDmesg", "dmesg");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdDf", "df -h");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCpuInfo", "cpuinfo");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdMpstat", "mpstat -P ALL 1 1");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdMpstat-I", "mpstat -I ALL 1 1");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdIrqInfo", "irq_info");
	jrpc_register_procedure(&my_server, run_builtin_cmd, "GetCmdCgtop", "cgtop");

	jrpc_register_procedure(&my_server, run_perf_report_cmd, "GetCmdPerfFaults", "perf record -a -e faults sleep 1");
	jrpc_register_procedure(&my_server, run_perf_report_cmd, "GetCmdPerfCpuclock", "perf record -a -e cpu-clock sleep 1");
	jrpc_register_procedure(&my_server, run_perf_script_cmd, "GetCmdPerfFlame", "perf record -F 99 -a -g -- sleep 1");
	jrpc_server_run(&my_server);
	jrpc_server_destroy(&my_server);
	return 0;
}
