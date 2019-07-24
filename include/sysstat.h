#ifndef SYSSTAT_H_
#define SYSSTAT_H_

int iostat_main(int argc, char **argv, int fd);
int mpstat_main(int argc, char **argv, int fd);
int cpuinfo_main(int argc, char **argv, int out_fd);
#endif
