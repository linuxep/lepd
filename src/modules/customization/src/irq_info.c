/*
 * Copyright (C) 2017 The Lep Open Source Project
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

int irq_info_main(int argc, char *argv[]){
	int intr = 10;
	int softirq = 100;
/*******************************
  1 read /proc/stat
  2 get irqs
    b = strstr(buff, "intr ");
    if(b) sscanf(b,  "intr %llu", &llbuf);
     *intr = llbuf;
  3 get softirqs
    b = strstr(buff, "softirq ");
    if(b) sscanf(b,  "softirq %llu", &llbuf);
    *sofrirq = llbuf;
  4 sleep 1 sec
  5 repeat
  6 irq2 - irq1, softirq2 - softirq2
  7 return
*******************************8**/
	printf("intr/sec:%d, softirq/sec:%d\n", intr, softirq);
	return 1;
}
