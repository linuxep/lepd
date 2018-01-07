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


#define BUFFSIZE (64*1024)
static unsigned long long sleep_time = 1;

int irq_info_main(int argc, char *argv[]){

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
**********************************/

        unsigned long long irq[2]= {0}, softirq[2]= {0};

        static int fd;
        const char *b = NULL;
        unsigned long long llbuf = 0;
        char buff[BUFFSIZE-1] = {0};

	fd = open("/proc/stat", O_RDONLY, 0);
        read(fd, buff, BUFFSIZE-1);
        b = strstr(buff, "intr");
        if (b)
                sscanf(b, "intr %Lu", &llbuf);
        irq[0] = llbuf;

        b = strstr(buff, "softirq");
        if (b)
                sscanf(b, "softirq %Lu", &llbuf);
        softirq[0] = llbuf;
        close(fd);

        sleep(sleep_time);

        fd = open("/proc/stat", O_RDONLY, 0);
	read(fd, buff, BUFFSIZE-1);
        b = strstr(buff, "intr");
        if (b)
        	sscanf(b, "intr %Lu", &llbuf);
        irq[1] = llbuf;

        b = strstr(buff, "softirq");
        if (b)
        	sscanf(b, "softirq %Lu", &llbuf);
        softirq[1] = llbuf;
        close(fd);

        printf("irq:%d/s softirq:%d/s \n", irq[1]-irq[0], softirq[1]-softirq[0]);
	return 1;
}
