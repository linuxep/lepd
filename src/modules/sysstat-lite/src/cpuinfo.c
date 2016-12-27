/*
 * cpuinfo: get cpu information
 * (C) 2016-2017 by Barry Song <baohua@linuxep.com>
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
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "rd_stats.h"

static void figureout_cpu(void)
{
	FILE *fp;
	char line[8192];
	char *cpu_name = NULL;

	if ((fp = fopen(CPUINFO, "r")) == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *s, *n;
		s = strstr(line, ":");
		n = strstr(s, "ARM");
		if(!n)
			n = strstr(s, "AArch64");
		if(!n)
			n = strstr(s, "Intel");
		if(!n)
			n = strstr(s, "AMD");
		if(n)	{
			/* +2 to skip ": " */
			cpu_name = s + 2;
			break;
		}
	}

	if(!cpu_name)
		cpu_name = "unknown";

	printf("cpu_name: %s", cpu_name);

	fclose(fp);
}

int cpuinfo_main(int argc, char **argv)
{
	int cpu_nr = 0;

	/* What is the highest processor number on this machine? */
	cpu_nr = get_cpu_nr(~0, TRUE);

	printf("cpunr: %d\n", cpu_nr);
	figureout_cpu();
	return 0;
}
