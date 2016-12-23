/*
 * iostat: report CPU and I/O statistics
 * (C) 1998-2016 by Sebastien GODARD (sysstat <at> orange.fr)
 *
 ***************************************************************************
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published  by  the *
 * Free Software Foundation; either version 2 of the License, or (at  your *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it  will  be  useful,  but *
 * WITHOUT ANY WARRANTY; without the implied warranty  of  MERCHANTABILITY *
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License *
 * for more details.                                                       *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA              *
 ***************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "version.h"
#include "iostat.h"
#include "common.h"
#include "ioconf.h"
#include "rd_stats.h"
#include "count.h"

#ifdef USE_NLS
#include <locale.h>
#include <libintl.h>
#define _(string) gettext(string)
#else
#define _(string) (string)
#endif

#define SCCSID "@(#)sysstat-" VERSION ": " __FILE__ " compiled " __DATE__ " " __TIME__
static char *sccsid(void) { return (SCCSID); }

struct stats_cpu *st_cpu_iostat[2];
static unsigned long long uptime_iostat[2]  = {0, 0};
static unsigned long long uptime_iostat0[2] = {0, 0};
struct io_stats *st_iodev_iostat[2];
struct io_hdr_stats *st_hdr_iodev_iostat;
struct io_dlist *st_dev_list_iostat;

/* Last group name entered on the command line */
char group_name_iostat[MAX_NAME_LEN];

int iodev_nr_iostat = 0;	/* Nb of devices and partitions found. Includes nb of device groups */
int group_nr_iostat = 0;	/* Nb of device groups */
static int cpu_nr_iostat = 0;		/* Nb of processors on the machine */
int dlist_idx_iostat = 0;	/* Nb of devices entered on the command line */
static int flags_iostat = 0;		/* Flag for common options and system state */
unsigned int dm_major_iostat;	/* Device-mapper major number */

static long interval_iostat = 0;
char timestamp_iostat[64];

/*
 ***************************************************************************
 * Print usage and exit.
 *
 * IN:
 * @progname	Name of sysstat command.
 ***************************************************************************
 */
static void usage(char *progname)
{
	fprintf(stderr, _("Usage: %s [ options ] [ <interval_iostat> [ <count> ] ]\n"),
		progname);
#ifdef DEBUG
	fprintf(stderr, _("Options are:\n"
			  "[ -c ] [ -d ] [ -h ] [ -k | -m ] [ -N ] [ -t ] [ -V ] [ -x ] [ -y ] [ -z ]\n"
			  "[ -j { ID | LABEL | PATH | UUID | ... } ] [ -o JSON ]\n"
			  "[ [ -H ] -g <group_name_iostat> ] [ -p [ <device> [,...] | ALL ] ]\n"
			  "[ <device> [...] | ALL ] [ --debuginfo ]\n"));
#else
	fprintf(stderr, _("Options are:\n"
			  "[ -c ] [ -d ] [ -h ] [ -k | -m ] [ -N ] [ -t ] [ -V ] [ -x ] [ -y ] [ -z ]\n"
			  "[ -j { ID | LABEL | PATH | UUID | ... } ] [ -o JSON ]\n"
			  "[ [ -H ] -g <group_name_iostat> ] [ -p [ <device> [,...] | ALL ] ]\n"
			  "[ <device> [...] | ALL ]\n"));
#endif
	exit(1);
}

/*
 ***************************************************************************
 * Set disk output unit. Unit will be kB/s unless POSIXLY_CORRECT
 * environment variable has been set, in which case the output will be
 * expressed in blocks/s.
 ***************************************************************************
 */
void set_disk_output_unit(void)
{
	if (DISPLAY_KILOBYTES(flags_iostat) || DISPLAY_MEGABYTES(flags_iostat))
		return;

	/* Check POSIXLY_CORRECT environment variable */
	if (getenv(ENV_POSIXLY_CORRECT) == NULL) {
		/* Variable not set: Unit is kB/s and not blocks/s */
		flags_iostat |= I_D_KILOBYTES;
	}
}

/*
 ***************************************************************************
 * SIGALRM signal handler. No need to reset the handler here.
 *
 * IN:
 * @sig	Signal number.
 ***************************************************************************
 */
static void alarm_handler(int sig)
{
	alarm(interval_iostat);
}

/*
 ***************************************************************************
 * Initialize stats common structures.
 ***************************************************************************
 */
void init_stats(void)
{
	int i;

	/* Allocate structures for CPUs "all" and 0 */
	for (i = 0; i < 2; i++) {
		if ((st_cpu_iostat[i] = (struct stats_cpu *) malloc(STATS_CPU_SIZE * 2)) == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_cpu_iostat[i], 0, STATS_CPU_SIZE * 2);
	}
}

/*
 ***************************************************************************
 * Set every device entry to unregistered status. But don't change status
 * for group entries (whose status is DISK_GROUP).
 *
 * IN:
 * @iodev_nr_iostat		Number of devices and partitions.
 * @st_hdr_iodev_iostat	Pointer on first structure describing a device/partition.
 ***************************************************************************
 */
void set_entries_unregistered(int iodev_nr_iostat, struct io_hdr_stats *st_hdr_iodev_iostat)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev_iostat;

	for (i = 0; i < iodev_nr_iostat; i++, shi++) {
		if (shi->status == DISK_REGISTERED) {
			shi->status = DISK_UNREGISTERED;
		}
	}
}

/*
 ***************************************************************************
 * Free unregistered entries (mark them as unused).
 *
 * IN:
 * @iodev_nr_iostat		Number of devices and partitions.
 * @st_hdr_iodev_iostat	Pointer on first structure describing a device/partition.
 ***************************************************************************
 */
void free_unregistered_entries(int iodev_nr_iostat, struct io_hdr_stats *st_hdr_iodev_iostat)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev_iostat;

	for (i = 0; i < iodev_nr_iostat; i++, shi++) {
		if (shi->status == DISK_UNREGISTERED) {
			shi->used = FALSE;
		}
	}
}

/*
 ***************************************************************************
 * Allocate and init I/O device structures.
 *
 * IN:
 * @dev_nr	Number of devices and partitions (also including groups
 *		if option -g has been used).
 ***************************************************************************
 */
void salloc_device(int dev_nr)
{
	int i;

	for (i = 0; i < 2; i++) {
		if ((st_iodev_iostat[i] =
		     (struct io_stats *) malloc(IO_STATS_SIZE * dev_nr)) == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_iodev_iostat[i], 0, IO_STATS_SIZE * dev_nr);
	}

	if ((st_hdr_iodev_iostat =
	     (struct io_hdr_stats *) malloc(IO_HDR_STATS_SIZE * dev_nr)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(st_hdr_iodev_iostat, 0, IO_HDR_STATS_SIZE * dev_nr);
}

/*
 ***************************************************************************
 * Allocate structures for devices entered on the command line.
 *
 * IN:
 * @list_len	Number of arguments on the command line.
 ***************************************************************************
 */
void salloc_dev_list(int list_len)
{
	if ((st_dev_list_iostat = (struct io_dlist *) malloc(IO_DLIST_SIZE * list_len)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(st_dev_list_iostat, 0, IO_DLIST_SIZE * list_len);
}

/*
 ***************************************************************************
 * Free structures used for devices entered on the command line.
 ***************************************************************************
 */
void sfree_dev_list(void)
{
	free(st_dev_list_iostat);
}

/*
 ***************************************************************************
 * Look for the device in the device list and store it if not found.
 *
 * IN:
 * @dlist_idx_iostat	Length of the device list.
 * @device_name	Name of the device.
 *
 * OUT:
 * @dlist_idx_iostat	Length of the device list.
 *
 * RETURNS:
 * Position of the device in the list.
 ***************************************************************************
 */
int update_dev_list(int *dlist_idx_iostat, char *device_name)
{
	int i;
	struct io_dlist *sdli = st_dev_list_iostat;

	for (i = 0; i < *dlist_idx_iostat; i++, sdli++) {
		if (!strcmp(sdli->dev_name, device_name))
			break;
	}

	if (i == *dlist_idx_iostat) {
		/*
		 * Device not found: Store it.
		 * Group names will be distinguished from real device names
		 * thanks to their names which begin with a space.
		 */
		(*dlist_idx_iostat)++;
		strncpy(sdli->dev_name, device_name, MAX_NAME_LEN - 1);
	}

	return i;
}

/*
 ***************************************************************************
 * Allocate and init structures, according to system state.
 ***************************************************************************
 */
void io_sys_init(void)
{
	/* Allocate and init stat common counters */
	init_stats();

	/* How many processors on this machine? */
	cpu_nr_iostat = get_cpu_nr(~0, FALSE);

	/* Get number of block devices and partitions in /proc/diskstats */
	if ((iodev_nr_iostat = get_diskstats_dev_nr(CNT_PART, CNT_ALL_DEV)) > 0) {
		flags_iostat |= I_F_HAS_DISKSTATS;
		iodev_nr_iostat += NR_DEV_PREALLOC;
	}

	if (!HAS_DISKSTATS(flags_iostat) ||
	    (DISPLAY_PARTITIONS(flags_iostat) && !DISPLAY_PART_ALL(flags_iostat))) {
		/*
		 * If /proc/diskstats exists but we also want stats for the partitions
		 * of a particular device, stats will have to be found in /sys. So we
		 * need to know if /sys is mounted or not, and set flags_iostat accordingly.
		 */

		/* Get number of block devices (and partitions) in sysfs */
		if ((iodev_nr_iostat = get_sysfs_dev_nr(DISPLAY_PARTITIONS(flags_iostat))) > 0) {
			flags_iostat |= I_F_HAS_SYSFS;
			iodev_nr_iostat += NR_DEV_PREALLOC;
		}
		else {
			fprintf(stderr, _("Cannot find disk data\n"));
			exit(2);
		}
	}

	/* Also allocate stat structures for "group" devices */
	iodev_nr_iostat += group_nr_iostat;

	/*
	 * Allocate structures for number of disks found, but also
	 * for groups of devices if option -g has been entered on the command line.
	 * iodev_nr_iostat must be <> 0.
	 */
	salloc_device(iodev_nr_iostat);
}

/*
 ***************************************************************************
 * When group stats are to be displayed (option -g entered on the command
 * line), save devices and group names in the io_hdr_stats structures. This
 * is normally done later when stats are actually read from /proc or /sys
 * files (via a call to save_stats() function), but here we want to make
 * sure that the structures are ordered and that each device belongs to its
 * proper group.
 * Note that we can still have an unexpected device that gets attached to a
 * group as devices can be registered or unregistered dynamically.
 ***************************************************************************
 */
void presave_device_list(void)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev_iostat;
	struct io_dlist *sdli = st_dev_list_iostat;

	if (dlist_idx_iostat>0) {
		/* First, save the last group name entered on the command line in the list */
		update_dev_list(&dlist_idx_iostat, group_name_iostat);

		/* Now save devices and group names in the io_hdr_stats structures */
		for (i = 0; (i < dlist_idx_iostat) && (i < iodev_nr_iostat); i++, shi++, sdli++) {
			strncpy(shi->name, sdli->dev_name, MAX_NAME_LEN);
			shi->name[MAX_NAME_LEN - 1] = '\0';
			shi->used = TRUE;
			if (shi->name[0] == ' ') {
				/* Current device name is in fact the name of a group */
				shi->status = DISK_GROUP;
			}
			else {
				shi->status = DISK_REGISTERED;
			}
		}
	}
	else {
		/*
		 * No device names have been entered on the command line but
		 * the name of a group. Save that name at the end of the
		 * data table so that all devices that will be read will be
		 * included in that group.
		 */
		shi += iodev_nr_iostat - 1;
		strncpy(shi->name, group_name_iostat, MAX_NAME_LEN);
		shi->name[MAX_NAME_LEN - 1] = '\0';
		shi->used = TRUE;
		shi->status = DISK_GROUP;
	}
}

/*
 ***************************************************************************
 * Free various structures.
 ***************************************************************************
*/
void io_sys_free(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		/* Free CPU structures */
		free(st_cpu_iostat[i]);

		/* Free I/O device structures */
		free(st_iodev_iostat[i]);
	}

	free(st_hdr_iodev_iostat);
}

/*
 ***************************************************************************
 * Save stats for current device or partition.
 *
 * IN:
 * @name		Name of the device/partition.
 * @curr		Index in array for current sample statistics.
 * @st_io		Structure with device or partition to save.
 * @iodev_nr_iostat		Number of devices and partitions.
 * @st_hdr_iodev_iostat	Pointer on structures describing a device/partition.
 *
 * OUT:
 * @st_hdr_iodev_iostat	Pointer on structures describing a device/partition.
 ***************************************************************************
 */
void save_stats(char *name, int curr, void *st_io, int iodev_nr_iostat,
		struct io_hdr_stats *st_hdr_iodev_iostat)
{
	int i;
	struct io_hdr_stats *st_hdr_iodev_iostat_i;
	struct io_stats *st_iodev_iostat_i;

	/* Look for device in data table */
	for (i = 0; i < iodev_nr_iostat; i++) {
		st_hdr_iodev_iostat_i = st_hdr_iodev_iostat + i;
		if (!strcmp(st_hdr_iodev_iostat_i->name, name)) {
			break;
		}
	}

	if (i == iodev_nr_iostat) {
		/*
		 * This is a new device: Look for an unused entry to store it.
		 * Thus we are able to handle dynamically registered devices.
		 */
		for (i = 0; i < iodev_nr_iostat; i++) {
			st_hdr_iodev_iostat_i = st_hdr_iodev_iostat + i;
			if (!st_hdr_iodev_iostat_i->used) {
				/* Unused entry found... */
				st_hdr_iodev_iostat_i->used = TRUE; /* Indicate it is now used */
				strncpy(st_hdr_iodev_iostat_i->name, name, MAX_NAME_LEN - 1);
				st_hdr_iodev_iostat_i->name[MAX_NAME_LEN - 1] = '\0';
				st_iodev_iostat_i = st_iodev_iostat[!curr] + i;
				memset(st_iodev_iostat_i, 0, IO_STATS_SIZE);
				break;
			}
		}
	}
	if (i < iodev_nr_iostat) {
		st_hdr_iodev_iostat_i = st_hdr_iodev_iostat + i;
		if (st_hdr_iodev_iostat_i->status == DISK_UNREGISTERED) {
			st_hdr_iodev_iostat_i->status = DISK_REGISTERED;
		}
		st_iodev_iostat_i = st_iodev_iostat[curr] + i;
		*st_iodev_iostat_i = *((struct io_stats *) st_io);
	}
	/*
	 * else it was a new device
	 * but there was no free structure to store it.
	 */
}

/*
 ***************************************************************************
 * Read sysfs stat for current block device or partition.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 * @filename	File name where stats will be read.
 * @dev_name	Device or partition name.
 *
 * RETURNS:
 * 0 if file couldn't be opened, 1 otherwise.
 ***************************************************************************
 */
int read_sysfs_file_stat(int curr, char *filename, char *dev_name)
{
	FILE *fp;
	struct io_stats sdev;
	int i;
	unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks;
	unsigned long rd_ios, rd_merges_or_rd_sec, wr_ios, wr_merges;
	unsigned long rd_sec_or_wr_ios, wr_sec, rd_ticks_or_wr_sec;

	/* Try to read given stat file */
	if ((fp = fopen(filename, "r")) == NULL)
		return 0;

	i = fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu %u %u %u %u",
		   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
		   &wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks);

	if (i == 11) {
		/* Device or partition */
		sdev.rd_ios     = rd_ios;
		sdev.rd_merges  = rd_merges_or_rd_sec;
		sdev.rd_sectors = rd_sec_or_wr_ios;
		sdev.rd_ticks   = (unsigned int) rd_ticks_or_wr_sec;
		sdev.wr_ios     = wr_ios;
		sdev.wr_merges  = wr_merges;
		sdev.wr_sectors = wr_sec;
		sdev.wr_ticks   = wr_ticks;
		sdev.ios_pgr    = ios_pgr;
		sdev.tot_ticks  = tot_ticks;
		sdev.rq_ticks   = rq_ticks;
	}
	else if (i == 4) {
		/* Partition without extended statistics */
		sdev.rd_ios     = rd_ios;
		sdev.rd_sectors = rd_merges_or_rd_sec;
		sdev.wr_ios     = rd_sec_or_wr_ios;
		sdev.wr_sectors = rd_ticks_or_wr_sec;
	}

	if ((i == 11) || !DISPLAY_EXTENDED(flags_iostat)) {
		/*
		 * In fact, we _don't_ save stats if it's a partition without
		 * extended stats and yet we want to display ext stats.
		 */
		save_stats(dev_name, curr, &sdev, iodev_nr_iostat, st_hdr_iodev_iostat);
	}

	fclose(fp);

	return 1;
}

/*
 ***************************************************************************
 * Read sysfs stats for all the partitions of a device.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 * @dev_name	Device name.
 ***************************************************************************
 */
void read_sysfs_dlist_part_stat(int curr, char *dev_name)
{
	DIR *dir;
	struct dirent *drd;
	char dfile[MAX_PF_NAME], filename[MAX_PF_NAME];

	snprintf(dfile, MAX_PF_NAME, "%s/%s", SYSFS_BLOCK, dev_name);
	dfile[MAX_PF_NAME - 1] = '\0';

	/* Open current device directory in /sys/block */
	if ((dir = opendir(dfile)) == NULL)
		return;

	/* Get current entry */
	while ((drd = readdir(dir)) != NULL) {
		if (!strcmp(drd->d_name, ".") || !strcmp(drd->d_name, ".."))
			continue;
		snprintf(filename, MAX_PF_NAME, "%s/%s/%s", dfile, drd->d_name, S_STAT);
		filename[MAX_PF_NAME - 1] = '\0';

		/* Read current partition stats */
		read_sysfs_file_stat(curr, filename, drd->d_name);
	}

	/* Close device directory */
	closedir(dir);
}

/*
 ***************************************************************************
 * Read stats from the sysfs filesystem for the devices entered on the
 * command line.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 ***************************************************************************
 */
void read_sysfs_dlist_stat(int curr)
{
	int dev, ok;
	char filename[MAX_PF_NAME];
	char *slash;
	struct io_dlist *st_dev_list_iostat_i;

	/* Every I/O device (or partition) is potentially unregistered */
	set_entries_unregistered(iodev_nr_iostat, st_hdr_iodev_iostat);

	for (dev = 0; dev < dlist_idx_iostat; dev++) {
		st_dev_list_iostat_i = st_dev_list_iostat + dev;

		/* Some devices may have a slash in their name (eg. cciss/c0d0...) */
		while ((slash = strchr(st_dev_list_iostat_i->dev_name, '/'))) {
			*slash = '!';
		}

		snprintf(filename, MAX_PF_NAME, "%s/%s/%s",
			 SYSFS_BLOCK, st_dev_list_iostat_i->dev_name, S_STAT);
		filename[MAX_PF_NAME - 1] = '\0';

		/* Read device stats */
		ok = read_sysfs_file_stat(curr, filename, st_dev_list_iostat_i->dev_name);

		if (ok && st_dev_list_iostat_i->disp_part) {
			/* Also read stats for its partitions */
			read_sysfs_dlist_part_stat(curr, st_dev_list_iostat_i->dev_name);
		}
	}

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr_iostat, st_hdr_iodev_iostat);
}

/*
 ***************************************************************************
 * Read stats from the sysfs filesystem for every block devices found.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 ***************************************************************************
 */
void read_sysfs_stat(int curr)
{
	DIR *dir;
	struct dirent *drd;
	char filename[MAX_PF_NAME];
	int ok;

	/* Every I/O device entry is potentially unregistered */
	set_entries_unregistered(iodev_nr_iostat, st_hdr_iodev_iostat);

	/* Open /sys/block directory */
	if ((dir = opendir(SYSFS_BLOCK)) != NULL) {

		/* Get current entry */
		while ((drd = readdir(dir)) != NULL) {
			if (!strcmp(drd->d_name, ".") || !strcmp(drd->d_name, ".."))
				continue;
			snprintf(filename, MAX_PF_NAME, "%s/%s/%s",
				 SYSFS_BLOCK, drd->d_name, S_STAT);
			filename[MAX_PF_NAME - 1] = '\0';

			/* If current entry is a directory, try to read its stat file */
			ok = read_sysfs_file_stat(curr, filename, drd->d_name);

			/*
			 * If '-p ALL' was entered on the command line,
			 * also try to read stats for its partitions
			 */
			if (ok && DISPLAY_PART_ALL(flags_iostat)) {
				read_sysfs_dlist_part_stat(curr, drd->d_name);
			}
		}

		/* Close /sys/block directory */
		closedir(dir);
	}

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr_iostat, st_hdr_iodev_iostat);
}

/*
 ***************************************************************************
 * Read stats from /proc/diskstats.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 ***************************************************************************
 */
void read_diskstats_stat(int curr)
{
	FILE *fp;
	char line[256], dev_name[MAX_NAME_LEN];
	char *dm_name;
	struct io_stats sdev;
	int i;
	unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks;
	unsigned long rd_ios, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, wr_ios;
	unsigned long wr_merges, rd_sec_or_wr_ios, wr_sec;
	char *ioc_dname;
	unsigned int major, minor;

	/* Every I/O device entry is potentially unregistered */
	set_entries_unregistered(iodev_nr_iostat, st_hdr_iodev_iostat);

	if ((fp = fopen(DISKSTATS, "r")) == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {

		/* major minor name rio rmerge rsect ruse wio wmerge wsect wuse running use aveq */
		i = sscanf(line, "%u %u %s %lu %lu %lu %lu %lu %lu %lu %u %u %u %u",
			   &major, &minor, dev_name,
			   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
			   &wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks);

		if (i == 14) {
			/* Device or partition */
			if (!dlist_idx_iostat && !DISPLAY_PARTITIONS(flags_iostat) &&
			    !is_device(dev_name, ACCEPT_VIRTUAL_DEVICES))
				continue;
			sdev.rd_ios     = rd_ios;
			sdev.rd_merges  = rd_merges_or_rd_sec;
			sdev.rd_sectors = rd_sec_or_wr_ios;
			sdev.rd_ticks   = (unsigned int) rd_ticks_or_wr_sec;
			sdev.wr_ios     = wr_ios;
			sdev.wr_merges  = wr_merges;
			sdev.wr_sectors = wr_sec;
			sdev.wr_ticks   = wr_ticks;
			sdev.ios_pgr    = ios_pgr;
			sdev.tot_ticks  = tot_ticks;
			sdev.rq_ticks   = rq_ticks;
		}
		else if (i == 7) {
			/* Partition without extended statistics */
			if (DISPLAY_EXTENDED(flags_iostat) ||
			    (!dlist_idx_iostat && !DISPLAY_PARTITIONS(flags_iostat)))
				continue;

			sdev.rd_ios     = rd_ios;
			sdev.rd_sectors = rd_merges_or_rd_sec;
			sdev.wr_ios     = rd_sec_or_wr_ios;
			sdev.wr_sectors = rd_ticks_or_wr_sec;
		}
		else
			/* Unknown entry: Ignore it */
			continue;

		if ((ioc_dname = ioc_name(major, minor)) != NULL) {
			if (strcmp(dev_name, ioc_dname) && strcmp(ioc_dname, K_NODEV)) {
				/*
				 * No match: Use name generated from sysstat.ioconf data
				 * (if different from "nodev") works around known issues
				 * with EMC PowerPath.
				 */
				strncpy(dev_name, ioc_dname, MAX_NAME_LEN - 1);
				dev_name[MAX_NAME_LEN - 1] = '\0';
			}
		}

		if ((DISPLAY_DEVMAP_NAME(flags_iostat)) && (major == dm_major_iostat)) {
			/*
			 * If the device is a device mapper device, try to get its
			 * assigned name of its logical device.
			 */
			dm_name = transform_devmapname(major, minor);
			if (dm_name) {
				strncpy(dev_name, dm_name, MAX_NAME_LEN - 1);
				dev_name[MAX_NAME_LEN - 1] = '\0';
			}
		}

		save_stats(dev_name, curr, &sdev, iodev_nr_iostat, st_hdr_iodev_iostat);
	}
	fclose(fp);

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr_iostat, st_hdr_iodev_iostat);
}

/*
 ***************************************************************************
 * Compute stats for device groups using stats of every device belonging
 * to each of these groups.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 ***************************************************************************
 */
void compute_device_groups_stats(int curr)
{
	struct io_stats gdev, *ioi;
	struct io_hdr_stats *shi = st_hdr_iodev_iostat;
	int i, nr_disks;

	memset(&gdev, 0, IO_STATS_SIZE);
	nr_disks = 0;

	for (i = 0; i < iodev_nr_iostat; i++, shi++) {
		if (shi->used && (shi->status == DISK_REGISTERED)) {
			ioi = st_iodev_iostat[curr] + i;

			if (!DISPLAY_UNFILTERED(flags_iostat)) {
				if (!ioi->rd_ios && !ioi->wr_ios)
					continue;
			}

			gdev.rd_ios     += ioi->rd_ios;
			gdev.rd_merges  += ioi->rd_merges;
			gdev.rd_sectors += ioi->rd_sectors;
			gdev.rd_ticks   += ioi->rd_ticks;
			gdev.wr_ios     += ioi->wr_ios;
			gdev.wr_merges  += ioi->wr_merges;
			gdev.wr_sectors += ioi->wr_sectors;
			gdev.wr_ticks   += ioi->wr_ticks;
			gdev.ios_pgr    += ioi->ios_pgr;
			gdev.tot_ticks  += ioi->tot_ticks;
			gdev.rq_ticks   += ioi->rq_ticks;
			nr_disks++;
		}
		else if (shi->status == DISK_GROUP) {
			save_stats(shi->name, curr, &gdev, iodev_nr_iostat, st_hdr_iodev_iostat);
			shi->used = nr_disks;
			nr_disks = 0;
			memset(&gdev, 0, IO_STATS_SIZE);
		}
	}
}

/*
 ***************************************************************************
 * Write current sample's timestamp_iostat, either in plain or JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @rectime	Current date and time.
 ***************************************************************************
 */
void write_sample_timestamp_iostat(int tab, struct tm *rectime, FILE* fp)
{
	if (DISPLAY_ISO(flags_iostat)) {
		strftime(timestamp_iostat, sizeof(timestamp_iostat), "%FT%T%z", rectime);
	}
	else {
		strftime(timestamp_iostat, sizeof(timestamp_iostat), "%x %X", rectime);
	}
	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		xprintf(tab, "\"timestamp_iostat\": \"%s\",", timestamp_iostat);
	}
	else {
		fprintf(fp,"%s\n", timestamp_iostat);
	}
}

/*
 ***************************************************************************
 * Display CPU utilization in plain format.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 * @itv		Interval of time.
 ***************************************************************************
 */
void write_plain_cpu_stat(int curr, unsigned long long itv, FILE* fp)
{
	fprintf(fp,"avg-cpu:  %%user   %%nice %%system %%iowait  %%steal   %%idle\n");

	fprintf(fp,"       ");
	cprintf_pc(fp,6, 7, 2,
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_user,   st_cpu_iostat[curr]->cpu_user,   itv),
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_nice,   st_cpu_iostat[curr]->cpu_nice,   itv),
		   /*
		    * Time spent in system mode also includes time spent servicing
		    * hard and soft interrupts.
		    */
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_sys + st_cpu_iostat[!curr]->cpu_softirq +
			       st_cpu_iostat[!curr]->cpu_hardirq,
			       st_cpu_iostat[curr]->cpu_sys + st_cpu_iostat[curr]->cpu_softirq +
			       st_cpu_iostat[curr]->cpu_hardirq, itv),
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_iowait, st_cpu_iostat[curr]->cpu_iowait, itv),
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_steal,  st_cpu_iostat[curr]->cpu_steal,  itv),
		   (st_cpu_iostat[curr]->cpu_idle < st_cpu_iostat[!curr]->cpu_idle) ?
		   0.0 :
		   ll_sp_value(st_cpu_iostat[!curr]->cpu_idle,   st_cpu_iostat[curr]->cpu_idle,   itv));

	fprintf(fp,"\n\n");
}

/*
 ***************************************************************************
 * Display CPU utilization in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @curr	Index in array for current sample statistics.
 * @itv		Interval of time.
 ***************************************************************************
 */
void write_json_cpu_stat(int tab, int curr, unsigned long long itv)
{
	xprintf0(tab, "\"avg-cpu\":  {\"user\": %.2f, \"nice\": %.2f, \"system\": %.2f,"
		      " \"iowait\": %.2f, \"steal\": %.2f, \"idle\": %.2f}",
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_user,   st_cpu_iostat[curr]->cpu_user,   itv),
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_nice,   st_cpu_iostat[curr]->cpu_nice,   itv),
		 /*
		  * Time spent in system mode also includes time spent servicing
		  * hard and soft interrupts.
		  */
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_sys + st_cpu_iostat[!curr]->cpu_softirq +
			     st_cpu_iostat[!curr]->cpu_hardirq,
			     st_cpu_iostat[curr]->cpu_sys + st_cpu_iostat[curr]->cpu_softirq +
			     st_cpu_iostat[curr]->cpu_hardirq, itv),
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_iowait, st_cpu_iostat[curr]->cpu_iowait, itv),
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_steal,  st_cpu_iostat[curr]->cpu_steal,  itv),
		 (st_cpu_iostat[curr]->cpu_idle < st_cpu_iostat[!curr]->cpu_idle) ?
		 0.0 :
		 ll_sp_value(st_cpu_iostat[!curr]->cpu_idle,   st_cpu_iostat[curr]->cpu_idle,   itv));
}

/*
 ***************************************************************************
 * Display CPU utilization in plain or JSON format.
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 * @itv		Interval of time.
 * @tab		Number of tabs to print (JSON format only).
 ***************************************************************************
 */
void write_cpu_stat(int curr, unsigned long long itv, int tab, FILE* fp)
{
	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		write_json_cpu_stat(tab, curr, itv);
	}
	else {
		write_plain_cpu_stat(curr, itv, fp);
	}
}

/*
 ***************************************************************************
 * Display disk stats header in plain or JSON format.
 *
 * OUT:
 * @fctr	Conversion factor.
 * @tab		Number of tabs to print (JSON format only).
 ***************************************************************************
 */
void write_disk_stat_header(int *fctr, int *tab, FILE* fp)
{
	if (DISPLAY_KILOBYTES(flags_iostat)) {
		*fctr = 2;
	}
	else if (DISPLAY_MEGABYTES(flags_iostat)) {
		*fctr = 2048;
	}

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		xprintf((*tab)++, "\"disk\": [");
		return;
	}

	if (DISPLAY_EXTENDED(flags_iostat)) {
		/* Extended stats */
		fprintf(fp,"Device:         rrqm/s   wrqm/s     r/s     w/s");
		if (DISPLAY_MEGABYTES(flags_iostat)) {
			fprintf(fp,"    rMB/s    wMB/s");
		}
		else if (DISPLAY_KILOBYTES(flags_iostat)) {
			fprintf(fp,"    rkB/s    wkB/s");
		}
		else {
			fprintf(fp,"   rsec/s   wsec/s");
		}
		fprintf(fp," avgrq-sz avgqu-sz   await r_await w_await  svctm  %%util\n");
	}
	else {
		/* Basic stats */
		fprintf(fp,"Device:            tps");
		if (DISPLAY_KILOBYTES(flags_iostat)) {
			fprintf(fp,"    kB_read/s    kB_wrtn/s    kB_read    kB_wrtn\n");
		}
		else if (DISPLAY_MEGABYTES(flags_iostat)) {
			fprintf(fp,"    MB_read/s    MB_wrtn/s    MB_read    MB_wrtn\n");
		}
		else {
			fprintf(fp,"   Blk_read/s   Blk_wrtn/s   Blk_read   Blk_wrtn\n");
		}
	}
}

/*
 ***************************************************************************
 * Display extended stats, read from /proc/{diskstats,partitions} or /sys,
 * in plain format.
 *
 * IN:
 *
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @shi		Structures describing the devices and partitions.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @devname	Current device name.
 * @xds		Extended stats for current device.
 * @r_await	r_await metric value.
 * @w_await	w_await metric value.
 ***************************************************************************
 */
void write_plain_ext_stat(unsigned long long itv, int fctr,
			  struct io_hdr_stats *shi, struct io_stats *ioi,
			  struct io_stats *ioj, char *devname, struct ext_disk_stats *xds,
			  double r_await, double w_await, FILE* fp)
{
	if (DISPLAY_HUMAN_READ(flags_iostat)) {
		cprintf_in(fp,IS_STR, "%s\n", devname, 0);
		fprintf(fp,"%13s", "");
	}
	else {
		cprintf_in(fp,IS_STR, "%-13s", devname, 0);
	}

	/*       rrq/s wrq/s   r/s   w/s  rsec  wsec  rqsz  qusz await r_await w_await svctm %util */
	cprintf_f(fp,2, 8, 2,
		  S_VALUE(ioj->rd_merges, ioi->rd_merges, itv),
		  S_VALUE(ioj->wr_merges, ioi->wr_merges, itv));
	cprintf_f(fp,2, 7, 2,
		  S_VALUE(ioj->rd_ios, ioi->rd_ios, itv),
		  S_VALUE(ioj->wr_ios, ioi->wr_ios, itv));
	cprintf_f(fp,4, 8, 2,
		  S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
		  S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr,
		  xds->arqsz,
		  S_VALUE(ioj->rq_ticks, ioi->rq_ticks, itv) / 1000.0);
	cprintf_f(fp,3, 7, 2, xds->await, r_await, w_await);
	/* The ticks output is biased to output 1000 ticks per second */
	cprintf_f(fp,1, 6, 2, xds->svctm);
	/*
	 * Again: Ticks in milliseconds.
	 * In the case of a device group (option -g), shi->used is the number of
	 * devices in the group. Else shi->used equals 1.
	 */
	cprintf_pc(fp,1, 6, 2,
		   shi->used ? xds->util / 10.0 / (double) shi->used
		             : xds->util / 10.0);	/* shi->used should never be zero here */
	fprintf(fp,"\n");
}

/*
 ***************************************************************************
 * Display extended stats, read from /proc/{diskstats,partitions} or /sys,
 * in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @shi		Structures describing the devices and partitions.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @devname	Current device name.
 * @xds		Extended stats for current device.
 * @r_await	r_await metric value.
 * @w_await	w_await metric value.
 ***************************************************************************
 */
void write_json_ext_stat(int tab, unsigned long long itv, int fctr,
		    struct io_hdr_stats *shi, struct io_stats *ioi,
		    struct io_stats *ioj, char *devname, struct ext_disk_stats *xds,
		    double r_await, double w_await)
{
	xprintf0(tab,
		 "{\"disk_device\": \"%s\", \"rrqm\": %.2f, \"wrqm\": %.2f, "
		 "\"r\": %.2f, \"w\": %.2f, \"rkB\": %.2f, \"wkB\": %.2f, "
		 "\"avgrq-sz\": %.2f, \"avgqu-sz\": %.2f, "
		 "\"await\": %.2f, \"r_await\": %.2f, \"w_await\": %.2f, "
		 "\"svctm\": %.2f, \"util\": %.2f}",
		 devname,
		 S_VALUE(ioj->rd_merges, ioi->rd_merges, itv),
		 S_VALUE(ioj->wr_merges, ioi->wr_merges, itv),
		 S_VALUE(ioj->rd_ios, ioi->rd_ios, itv),
		 S_VALUE(ioj->wr_ios, ioi->wr_ios, itv),
		 S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
		 S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr,
		 xds->arqsz,
		 S_VALUE(ioj->rq_ticks, ioi->rq_ticks, itv) / 1000.0,
		 xds->await,
		 r_await,
		 w_await,
		 xds->svctm,
		 shi->used ? xds->util / 10.0 / (double) shi->used
			   : xds->util / 10.0);	/* shi->used should never be zero here */
}

/*
 ***************************************************************************
 * Display extended stats, read from /proc/{diskstats,partitions} or /sys,
 * in plain or JSON format.
 *
 * IN:
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @shi		Structures describing the devices and partitions.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @tab		Number of tabs to print (JSON output only).
 ***************************************************************************
 */
void write_ext_stat(unsigned long long itv, int fctr,
		    struct io_hdr_stats *shi, struct io_stats *ioi,
		    struct io_stats *ioj, int tab, FILE* fp)
{
	char *devname = NULL;
	struct stats_disk sdc, sdp;
	struct ext_disk_stats xds;
	double r_await, w_await;

	/*
	 * Counters overflows are possible, but don't need to be handled in
	 * a special way: The difference is still properly calculated if the
	 * result is of the same type as the two values.
	 * Exception is field rq_ticks which is incremented by the number of
	 * I/O in progress times the number of milliseconds spent doing I/O.
	 * But the number of I/O in progress (field ios_pgr) happens to be
	 * sometimes negative...
	 */
	sdc.nr_ios    = ioi->rd_ios + ioi->wr_ios;
	sdp.nr_ios    = ioj->rd_ios + ioj->wr_ios;

	sdc.tot_ticks = ioi->tot_ticks;
	sdp.tot_ticks = ioj->tot_ticks;

	sdc.rd_ticks  = ioi->rd_ticks;
	sdp.rd_ticks  = ioj->rd_ticks;
	sdc.wr_ticks  = ioi->wr_ticks;
	sdp.wr_ticks  = ioj->wr_ticks;

	sdc.rd_sect   = ioi->rd_sectors;
	sdp.rd_sect   = ioj->rd_sectors;
	sdc.wr_sect   = ioi->wr_sectors;
	sdp.wr_sect   = ioj->wr_sectors;

	compute_ext_disk_stats(&sdc, &sdp, itv, &xds);

	r_await = (ioi->rd_ios - ioj->rd_ios) ?
		  (ioi->rd_ticks - ioj->rd_ticks) /
		  ((double) (ioi->rd_ios - ioj->rd_ios)) : 0.0;
	w_await = (ioi->wr_ios - ioj->wr_ios) ?
		  (ioi->wr_ticks - ioj->wr_ticks) /
		  ((double) (ioi->wr_ios - ioj->wr_ios)) : 0.0;

	/* Get device name */
	if (DISPLAY_PERSIST_NAME_I(flags_iostat)) {
		devname = get_persistent_name_from_pretty(shi->name);
	}
	if (!devname) {
		devname = shi->name;
	}

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		write_json_ext_stat(tab, itv, fctr, shi, ioi, ioj, devname, &xds,
				    r_await, w_await);
	}
	else {
		write_plain_ext_stat(itv, fctr, shi, ioi, ioj, devname, &xds,
				     r_await, w_await, fp);
	}
}

/*
 ***************************************************************************
 * Write basic stats, read from /proc/diskstats or from sysfs, in plain
 * format.
 *
 * IN:
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @devname	Current device name.
 * @rd_sec	Number of sectors read.
 * @wr_sec	Number of sectors written.
 ***************************************************************************
 */
void write_plain_basic_stat(unsigned long long itv, int fctr,
			    struct io_stats *ioi, struct io_stats *ioj,
			    char *devname, unsigned long long rd_sec,
			    unsigned long long wr_sec, FILE* fp)
{
	if (DISPLAY_HUMAN_READ(flags_iostat)) {
		cprintf_in(fp,IS_STR, "%s\n", devname, 0);
		fprintf(fp,"%13s", "");
	}
	else {
		cprintf_in(fp,IS_STR, "%-13s", devname, 0);
	}
	cprintf_f(fp,1, 8, 2,
		  S_VALUE(ioj->rd_ios + ioj->wr_ios, ioi->rd_ios + ioi->wr_ios, itv));
	cprintf_f(fp,2, 12, 2,
		  S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
		  S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr);
	cprintf_u64(fp,2, 10,
		    (unsigned long long) rd_sec / fctr,
		    (unsigned long long) wr_sec / fctr);
	fprintf(fp,"\n");
}

/*
 ***************************************************************************
 * Write basic stats, read from /proc/diskstats or from sysfs, in JSON
 * format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @devname	Current device name.
 * @rd_sec	Number of sectors read.
 * @wr_sec	Number of sectors written.
 ***************************************************************************
 */
void write_json_basic_stat(int tab, unsigned long long itv, int fctr,
			   struct io_stats *ioi, struct io_stats *ioj,
			   char *devname, unsigned long long rd_sec,
			   unsigned long long wr_sec)
{
	xprintf0(tab,
		 "{\"disk_device\": \"%s\", \"tps\": %.2f, "
		 "\"kB_read_per_sec\": %.2f, \"kB_wrtn_per_sec\": %.2f, "
		 "\"kB_read\": %llu, \"kB_wrtn\": %llu}",
		 devname,
		 S_VALUE(ioj->rd_ios + ioj->wr_ios, ioi->rd_ios + ioi->wr_ios, itv),
		 S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
		 S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr,
		 (unsigned long long) rd_sec / fctr,
		 (unsigned long long) wr_sec / fctr);
}

/*
 ***************************************************************************
 * Write basic stats, read from /proc/diskstats or from sysfs, in plain or
 * JSON format.
 *
 * IN:
 * @itv		Interval of time.
 * @fctr	Conversion factor.
 * @shi		Structures describing the devices and partitions.
 * @ioi		Current sample statistics.
 * @ioj		Previous sample statistics.
 * @tab		Number of tabs to print (JSON format only).
 ***************************************************************************
 */
void write_basic_stat(unsigned long long itv, int fctr,
		      struct io_hdr_stats *shi, struct io_stats *ioi,
		      struct io_stats *ioj, int tab, FILE* fp)
{
	char *devname = NULL;
	unsigned long long rd_sec, wr_sec;

	/* Print device name */
	if (DISPLAY_PERSIST_NAME_I(flags_iostat)) {
		devname = get_persistent_name_from_pretty(shi->name);
	}
	if (!devname) {
		devname = shi->name;
	}

	/* Print stats coming from /sys or /proc/diskstats */
	rd_sec = ioi->rd_sectors - ioj->rd_sectors;
	if ((ioi->rd_sectors < ioj->rd_sectors) && (ioj->rd_sectors <= 0xffffffff)) {
		rd_sec &= 0xffffffff;
	}
	wr_sec = ioi->wr_sectors - ioj->wr_sectors;
	if ((ioi->wr_sectors < ioj->wr_sectors) && (ioj->wr_sectors <= 0xffffffff)) {
		wr_sec &= 0xffffffff;
	}

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		write_json_basic_stat(tab, itv, fctr, ioi, ioj, devname,
				      rd_sec, wr_sec);
	}
	else {
		write_plain_basic_stat(itv, fctr, ioi, ioj, devname,
				       rd_sec, wr_sec, fp);
	}
}

/*
 ***************************************************************************
 * Print everything now (stats and uptime_iostat).
 *
 * IN:
 * @curr	Index in array for current sample statistics.
 * @rectime	Current date and time.
 ***************************************************************************
 */
static void write_stats(int curr, struct tm *rectime, FILE* fp)
{
	int dev, i, fctr = 1, tab = 4, next = FALSE;
	unsigned long long itv;
	struct io_hdr_stats *shi;
	struct io_dlist *st_dev_list_iostat_i;

	/* Test stdout */
	TEST_STDOUT(STDOUT_FILENO);

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		xprintf(tab++, "{");
	}

	/* Print time stamp */
	if (DISPLAY_TIMESTAMP(flags_iostat)) {
		write_sample_timestamp_iostat(tab, rectime, fp);
#ifdef DEBUG
		if (DISPLAY_DEBUG(flags_iostat)) {
			fprintf(stderr, "%s\n", timestamp_iostat);
		}
#endif
	}

	/* Interval is multiplied by the number of processors */
	itv = get_interval(uptime_iostat[!curr], uptime_iostat[curr]);

	if (DISPLAY_CPU(flags_iostat)) {
#ifdef DEBUG
		if (DISPLAY_DEBUG(flags_iostat)) {
			/* Debug output */
			fprintf(stderr, "itv=%llu st_cpu_iostat[curr]{ cpu_user=%llu cpu_nice=%llu "
					"cpu_sys=%llu cpu_idle=%llu cpu_iowait=%llu cpu_steal=%llu "
					"cpu_hardirq=%llu cpu_softirq=%llu cpu_guest=%llu "
					"cpu_guest_nice=%llu }\n",
				itv,
				st_cpu_iostat[curr]->cpu_user,
				st_cpu_iostat[curr]->cpu_nice,
				st_cpu_iostat[curr]->cpu_sys,
				st_cpu_iostat[curr]->cpu_idle,
				st_cpu_iostat[curr]->cpu_iowait,
				st_cpu_iostat[curr]->cpu_steal,
				st_cpu_iostat[curr]->cpu_hardirq,
				st_cpu_iostat[curr]->cpu_softirq,
				st_cpu_iostat[curr]->cpu_guest,
				st_cpu_iostat[curr]->cpu_guest_nice);
		}
#endif

		/* Display CPU utilization */
		write_cpu_stat(curr, itv, tab, fp);

		if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
			if (DISPLAY_DISK(flags_iostat)) {
				printf(",");
			}
			printf("\n");
		}
	}

	if (cpu_nr_iostat > 1) {
		/* On SMP machines, reduce itv to one processor (see note above) */
		itv = get_interval(uptime_iostat0[!curr], uptime_iostat0[curr]);
	}

	if (DISPLAY_DISK(flags_iostat)) {
		struct io_stats *ioi, *ioj;

		shi = st_hdr_iodev_iostat;

		/* Display disk stats header */
		write_disk_stat_header(&fctr, &tab, fp);

		for (i = 0; i < iodev_nr_iostat; i++, shi++) {
			if (shi->used) {

				if (dlist_idx_iostat && !HAS_SYSFS(flags_iostat)) {
					/*
					 * With /proc/diskstats, stats for every device
					 * are read even if we have entered a list on devices
					 * on the command line. Thus we need to check
					 * if stats for current device are to be displayed.
					 */
					for (dev = 0; dev < dlist_idx_iostat; dev++) {
						st_dev_list_iostat_i = st_dev_list_iostat + dev;
						if (!strcmp(shi->name, st_dev_list_iostat_i->dev_name))
							break;
					}
					if (dev == dlist_idx_iostat)
						/* Device not found in list: Don't display it */
						continue;
				}

				ioi = st_iodev_iostat[curr] + i;
				ioj = st_iodev_iostat[!curr] + i;

				if (!DISPLAY_UNFILTERED(flags_iostat)) {
					if (!ioi->rd_ios && !ioi->wr_ios)
						continue;
				}

				if (DISPLAY_ZERO_OMIT(flags_iostat)) {
					if ((ioi->rd_ios == ioj->rd_ios) &&
						(ioi->wr_ios == ioj->wr_ios))
						/* No activity: Ignore it */
						continue;
				}

				if (DISPLAY_GROUP_TOTAL_ONLY(flags_iostat)) {
					if (shi->status != DISK_GROUP)
						continue;
				}
#ifdef DEBUG
				if (DISPLAY_DEBUG(flags_iostat)) {
					/* Debug output */
					fprintf(stderr, "name=%s itv=%llu fctr=%d ioi{ rd_sectors=%lu "
							"wr_sectors=%lu rd_ios=%lu rd_merges=%lu rd_ticks=%u "
							"wr_ios=%lu wr_merges=%lu wr_ticks=%u ios_pgr=%u tot_ticks=%u "
							"rq_ticks=%u }\n",
						shi->name,
						itv,
						fctr,
						ioi->rd_sectors,
						ioi->wr_sectors,
						ioi->rd_ios,
						ioi->rd_merges,
						ioi->rd_ticks,
						ioi->wr_ios,
						ioi->wr_merges,
						ioi->wr_ticks,
						ioi->ios_pgr,
						ioi->tot_ticks,
						ioi->rq_ticks
						);
				}
#endif

				if (DISPLAY_JSON_OUTPUT(flags_iostat) && next) {
					printf(",\n");
				}
				next = TRUE;

				if (DISPLAY_EXTENDED(flags_iostat)) {
					write_ext_stat(itv, fctr, shi, ioi, ioj, tab, fp);
				}
				else {
					write_basic_stat(itv, fctr, shi, ioi, ioj, tab, fp);
				}
			}
		}
		if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
			printf("\n");
			xprintf(--tab, "]");
		}
	}

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		xprintf0(--tab, "}");
	}
	else {
		fprintf(fp, "\n");
	}
}

/*
 ***************************************************************************
 * Main loop: Read I/O stats from the relevant sources and display them.
 *
 * IN:
 * @count	Number of reports to print.
 * @rectime	Current date and time.
 ***************************************************************************
 */
void rw_io_stat_loop(long int count, struct tm *rectime, FILE* fp)
{
	int curr = 1;
	int skip = 0;

	/* Should we skip first report? */
	if (DISPLAY_OMIT_SINCE_BOOT(flags_iostat) && interval_iostat > 0) {
		skip = 1;
	}

	/* Don't buffer data if redirected to a pipe */
	setbuf(stdout, NULL);

	do {
		if (cpu_nr_iostat > 1) {
			/*
			 * Read system uptime_iostat (only for SMP machines).
			 * Init uptime_iostat0. So if /proc/uptime_iostat cannot fill it,
			 * this will be done by /proc/stat.
			 */
			uptime_iostat0[curr] = 0;
			read_uptime(&(uptime_iostat0[curr]));
		}

		/*
		 * Read stats for CPU "all" and 0.
		 * Note that stats for CPU 0 are not used per se. It only makes
		 * read_stat_cpu() fill uptime_iostat0.
		 */
		read_stat_cpu(st_cpu_iostat[curr], 2, &(uptime_iostat[curr]), &(uptime_iostat0[curr]));

		if (dlist_idx_iostat) {
			/*
			 * A device or partition name was explicitly entered
			 * on the command line, with or without -p option
			 * (but not -p ALL).
			 */
			if (HAS_DISKSTATS(flags_iostat) && !DISPLAY_PARTITIONS(flags_iostat)) {
				read_diskstats_stat(curr);
			}
			else if (HAS_SYSFS(flags_iostat)) {
				read_sysfs_dlist_stat(curr);
			}
		}
		else {
			/*
			 * No devices nor partitions entered on the command line
			 * (for example if -p ALL was used).
			 */
			if (HAS_DISKSTATS(flags_iostat)) {
				read_diskstats_stat(curr);
			}
			else if (HAS_SYSFS(flags_iostat)) {
				read_sysfs_stat(curr);
			}
		}

		/* Compute device groups stats */
		if (group_nr_iostat > 0) {
			compute_device_groups_stats(curr);
		}

		/* Get time */
		get_localtime(rectime, 0);

		/* Check whether we should skip first report */
		if (!skip) {
			/* Print results */
			write_stats(curr, rectime, fp);

			if (count > 0) {
				count--;
			}
		}
		else {
			skip = 0;
		}

		if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
			if (count) {
			printf(",");
			}
			printf("\n");
		}
		if (count) {
			curr ^= 1;
			pause();
		}
	}
	while (count);

	if (DISPLAY_JSON_OUTPUT(flags_iostat)) {
		printf("\t\t\t]\n\t\t}\n\t]\n}}\n");
	}
}

/*
 ***************************************************************************
 * Main entry to the iostat program.
 ***************************************************************************
 */
int iostat_main(int argc, char **argv, int fd)
{
	int it = 0;
	int opt = 1;
	int i, report_set = FALSE;
	long count = 1;
	struct utsname header;
	struct io_dlist *st_dev_list_iostat_i;
	struct tm rectime;
	char *t, *persist_devname, *devname;

#ifdef USE_NLS
	/* Init National Language Support */
	init_nls();
#endif

	/* Init color strings */
	init_colors();

	/* Get HZ */
	get_HZ();

	/* Allocate structures for device list */
	if (argc > 1) {
		salloc_dev_list(argc - 1 + count_csvalues(argc, argv));
	}

	/* Process args... */
	while (opt < argc) {

		/* -p option used individually. See below for grouped use */
		if (!strcmp(argv[opt], "-p")) {
			flags_iostat |= I_D_PARTITIONS;
			if (argv[++opt] &&
			    (strspn(argv[opt], DIGITS) != strlen(argv[opt])) &&
			    (strncmp(argv[opt], "-", 1))) {
				flags_iostat |= I_D_UNFILTERED;

				for (t = strtok(argv[opt], ","); t; t = strtok(NULL, ",")) {
					if (!strcmp(t, K_ALL)) {
						flags_iostat |= I_D_PART_ALL;
					}
					else {
						devname = device_name(t);
						if (DISPLAY_PERSIST_NAME_I(flags_iostat)) {
							/* Get device persistent name */
							persist_devname = get_pretty_name_from_persistent(devname);
							if (persist_devname != NULL) {
								devname = persist_devname;
							}
						}
						/* Store device name */
						i = update_dev_list(&dlist_idx_iostat, devname);
						st_dev_list_iostat_i = st_dev_list_iostat + i;
						st_dev_list_iostat_i->disp_part = TRUE;
					}
				}
				opt++;
			}
			else {
				flags_iostat |= I_D_PART_ALL;
			}
		}

		else if (!strcmp(argv[opt], "-g")) {
			/*
			 * Option -g: Stats for a group of devices.
			 * group_name_iostat contains the last group name entered on
			 * the command line. If we define an additional one, save
			 * the previous one in the list. We do that this way because we
			 * want the group name to appear in the list _after_ all
			 * the devices included in that group. The last group name
			 * will be saved in the list later, in presave_device_list() function.
			 */
			if (group_nr_iostat > 0) {
				update_dev_list(&dlist_idx_iostat, group_name_iostat);
			}
			if (argv[++opt]) {
				/*
				 * MAX_NAME_LEN - 2: one char for the heading space,
				 * and one for the trailing '\0'.
				 */
				snprintf(group_name_iostat, MAX_NAME_LEN, " %-.*s", MAX_NAME_LEN - 2, argv[opt++]);
			}
			else {
				usage(argv[0]);
			}
			group_nr_iostat++;
		}

		else if (!strcmp(argv[opt], "-j")) {
			if (argv[++opt]) {
				if (strnlen(argv[opt], MAX_FILE_LEN) >= MAX_FILE_LEN - 1) {
					usage(argv[0]);
				}
				strncpy(persistent_name_type, argv[opt], MAX_FILE_LEN - 1);
				persistent_name_type[MAX_FILE_LEN - 1] = '\0';
				strtolower(persistent_name_type);
				/* Check that this is a valid type of persistent device name */
				if (!get_persistent_type_dir(persistent_name_type)) {
					fprintf(stderr, _("Invalid type of persistent device name\n"));
					exit(1);
				}
				/*
				 * Persistent names are usually long: Display
				 * them as human readable by default.
				 */
				flags_iostat |= I_D_PERSIST_NAME + I_D_HUMAN_READ;
				opt++;
			}
			else {
				usage(argv[0]);
			}
		}

		else if (!strcmp(argv[opt], "-o")) {
			/* Select output format */
			if (argv[++opt] && !strcmp(argv[opt], K_JSON)) {
				flags_iostat |= I_D_JSON_OUTPUT;
				opt++;
			}
			else {
				usage(argv[0]);
			}
		}

#ifdef DEBUG
		else if (!strcmp(argv[opt], "--debuginfo")) {
			flags_iostat |= I_D_DEBUG;
			opt++;
		}
#endif

		else if (!strncmp(argv[opt], "-", 1)) {
			for (i = 1; *(argv[opt] + i); i++) {

				switch (*(argv[opt] + i)) {

				case 'c':
					/* Display cpu usage */
					flags_iostat |= I_D_CPU;
					report_set = TRUE;
					break;

				case 'd':
					/* Display disk utilization */
					flags_iostat |= I_D_DISK;
					report_set = TRUE;
					break;

                                case 'H':
					/* Display stats only for the groups */
					flags_iostat |= I_D_GROUP_TOTAL_ONLY;
					break;

				case 'h':
					/*
					 * Display device utilization report
					 * in a human readable format.
					 */
					flags_iostat |= I_D_HUMAN_READ;
					break;

				case 'k':
					if (DISPLAY_MEGABYTES(flags_iostat)) {
						usage(argv[0]);
					}
					/* Display stats in kB/s */
					flags_iostat |= I_D_KILOBYTES;
					break;

				case 'm':
					if (DISPLAY_KILOBYTES(flags_iostat)) {
						usage(argv[0]);
					}
					/* Display stats in MB/s */
					flags_iostat |= I_D_MEGABYTES;
					break;

				case 'N':
					/* Display device mapper logical name */
					flags_iostat |= I_D_DEVMAP_NAME;
					break;

				case 'p':
					/* If option -p is grouped then it cannot take an arg */
					flags_iostat |= I_D_PARTITIONS + I_D_PART_ALL;
					break;

				case 't':
					/* Display timestamp_iostat */
					flags_iostat |= I_D_TIMESTAMP;
					break;

				case 'x':
					/* Display extended stats */
					flags_iostat |= I_D_EXTENDED;
					break;

				case 'y':
					/* Don't display stats since system restart */
					flags_iostat |= I_D_OMIT_SINCE_BOOT;
					break;

				case 'z':
					/* Omit output for devices with no activity */
					flags_iostat |= I_D_ZERO_OMIT;
					break;

				case 'V':
					/* Print version number and exit */
					print_version();
					break;

				default:
					usage(argv[0]);
				}
			}
			opt++;
		}

		else if (!isdigit(argv[opt][0])) {
			/*
			 * By default iostat doesn't display unused devices.
			 * If some devices are explicitly entered on the command line
			 * then don't apply this rule any more.
			 */
			flags_iostat |= I_D_UNFILTERED;

			if (strcmp(argv[opt], K_ALL)) {
				/* Store device name entered on the command line */
				devname = device_name(argv[opt++]);
				if (DISPLAY_PERSIST_NAME_I(flags_iostat)) {
					persist_devname = get_pretty_name_from_persistent(devname);
					if (persist_devname != NULL) {
						devname = persist_devname;
					}
				}
				update_dev_list(&dlist_idx_iostat, devname);
			}
			else {
				opt++;
			}
		}

		else if (!it) {
			interval_iostat = atol(argv[opt++]);
			if (interval_iostat < 0) {
				usage(argv[0]);
			}
			count = -1;
			it = 1;
		}

		else if (it > 0) {
			count = atol(argv[opt++]);
			if ((count < 1) || !interval_iostat) {
				usage(argv[0]);
			}
			it = -1;
		}
		else {
			usage(argv[0]);
		}
	}

	if (!interval_iostat) {
		count = 1;
	}

	/* Default: Display CPU and DISK reports */
	if (!report_set) {
		flags_iostat |= I_D_CPU + I_D_DISK;
	}
	/*
	 * Also display DISK reports if options -p, -x or a device has been entered
	 * on the command line.
	 */
	if (DISPLAY_PARTITIONS(flags_iostat) || DISPLAY_EXTENDED(flags_iostat) ||
	    DISPLAY_UNFILTERED(flags_iostat)) {
		flags_iostat |= I_D_DISK;
	}

	/* Option -T can only be used with option -g */
	if (DISPLAY_GROUP_TOTAL_ONLY(flags_iostat) && !group_nr_iostat) {
		usage(argv[0]);
	}

	/* Select disk output unit (kB/s or blocks/s) */
	set_disk_output_unit();

	/* Ignore device list if '-p ALL' entered on the command line */
	if (DISPLAY_PART_ALL(flags_iostat)) {
		dlist_idx_iostat = 0;
	}

	if (DISPLAY_DEVMAP_NAME(flags_iostat)) {
		dm_major_iostat = get_devmap_major();
	}

	/* Init structures according to machine architecture */
	io_sys_init();
	if (group_nr_iostat > 0) {
		/*
		 * If groups of devices have been defined
		 * then save devices and groups in the list.
		 */
		presave_device_list();
	}

	get_localtime(&rectime, 0);

	FILE* fp = fdopen(fd, "w");

	/* Get system name, release number and hostname */
	uname(&header);
	if (print_gal_header(&rectime, header.sysname, header.release,
			     header.nodename, header.machine, cpu_nr_iostat,
			     DISPLAY_JSON_OUTPUT(flags_iostat), fp)) {
		flags_iostat |= I_D_ISO;
	}
	if (!DISPLAY_JSON_OUTPUT(flags_iostat)) {
		fprintf(fp,"\n");
	}


	/* Main loop */
	rw_io_stat_loop(count, &rectime, fp);

	/* Free structures */
	io_sys_free();
	sfree_dev_list();
	fclose(fp);
	return 0;
}
