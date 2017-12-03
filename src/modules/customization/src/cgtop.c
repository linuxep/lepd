
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> 
#include <stddef.h> 
#include <time.h>

#define bool char 
#define true 1
#define false 0
#define uint64_t unsigned long long int
static char *fn = NULL;

struct Cgroup {
	char *path;
	int tasks;
	
	double cpu_fraction;
	
	uint64_t memory;
	
	uint64_t io_in_bps, io_out_bps;
	struct Cgroup *next;
	struct Cgroup *prev;
};

struct Cgroup *G;

struct timespec old_time;
unsigned long long int old_usage;
uint64_t old_rd, old_wr;

char * strjoin(const char *x, ...) {
	char *r;
	size_t l;
	va_list ap;
	
	va_start(ap, x);

	if (x) {
		l = strlen(x);

		for(;;) {
			const char *t;
			size_t n;
		
			t = va_arg(ap, const char *);
			if (!t)
				break;
			
			n = strlen(t);

			l += n;
		}
	}
	else
		l = 0;
	va_end(ap);

	r = (char *)malloc(sizeof(char) * (l+1));
	if (!r)
		return NULL;
	memset(r, 0, strlen(r));

	if (x) {
		strcat(r, x);
		
		va_start(ap, x);
		for(;;) {
			const char *t;
			
			t = va_arg(ap, const char *);
			if (!t)
				break;

			strcat(r, t);
		}
		va_end(ap);
	}
	else 
		r[0] = 0;

	return r;
};


char * path_kill_slashes(char * path) {
	
	char *f, *p;
	int i = 0, j = 0, x = 0, y = 0;

	j = strlen(path);
	p = f = (char *)malloc(sizeof(char) * j);
	memset(f, 0, j);

	*f = *path;

	while(*path) {		

		if (*path == '/' && *(path+1) == '\0') {
				i++;
				*(f+i) = '\0';
				x++;
		}
		
		if (*path == '/') {
			if (*(f+i) == '/') {
				x++;
			}
			else {
				if (*(path+1) == '/')
					x++;
				else {
					i++;
					*(f+i) = *path;
				}
			}
		}
		
		if (*path != '/') {
			i++;
			*(f+i) = *path;			
		}	

		path++;
		
	}
			
	y = j-x+1;	
	p = p+y;
	while(*p) {
		*p = '\0';
	}

	return f;
}

static int join_path(const char *controller, const char *path, const char *suffix, char **fs) {
	char *t = NULL;

	t = strjoin("/sys/fs/cgroup/", controller, "/", path, "/", suffix, NULL);
	if (!t)
		return -1;
	
	*fs = path_kill_slashes(t);

	return 0;
}


int cg_get_path(const char *controller, const char *path, const char *suffix, char **fs) {
	return join_path(controller, path, suffix, fs);
}


char * cg_enumerate_processes(const char *controller, const char *path) {
	
	char *fs = NULL;
	FILE *f;
	int r;

	r =cg_get_path(controller, path, "cgroup.procs", &fs);
	if (r < 0)
		return NULL;

	return fs;
}


int cg_read_pid(FILE *f, pid_t *_pid) {
	unsigned long ul;
	
	if (fscanf(f, "%lu", &ul) != 1) {
		if (feof(f))
			return -1;
	}

	*_pid = (pid_t) ul;
	return 0;
}

int read_one_line_file(const char *fn, char **line) {
	FILE *f = NULL;
	char t[20] = {0}, *c;
	
	f = fopen(fn, "re");
	if (!f)
		return -1;
	
	if (!fgets(t, sizeof(t), f))
		return -1;
	
	c = strdup(t);
	if (!c)
		return -1;
	
	c[strcspn(c, "\n\r")] = 0;
	
	*line = c;
	return 0;
}

char *strstrip(char *s) {
	char *e;
	
	s += strspn(s, " \t\n\r");
	
	for(e = strchr(s, 0); e > s; e--)
		if (!strchr(" \t\n\r", e[-1]))
			break;
	
	*e = 0;
	return s;
}

char *first_word(const char *s, const char *word) {

	size_t sl, wl;
	const char *p;

	sl = strlen(s);
	wl = strlen(word);

	if (sl < wl)
		return  NULL;
	
	if (wl == 0)
		return s;

	if (memcmp(s, word, wl) != 0)
		return NULL;
	
	p = s+wl;
	if (*p == 0)
		return p;

	if (!strchr(" \t\n\r", *p))
		return NULL;

	p += strspn(p, " \t\n\r");
	return p;
}


static int process(const char *controller, const char *path, unsigned iteration) { 
	
	int r;
	FILE *f = NULL;
	pid_t pid;
	unsigned n = 0;
	char *fs = NULL;
	
	char *start = "/sys/fs/cgroup/systemd/";
	char *end = "/cgroup.procs";
	int a = 0;
	int b = 0;

	struct Cgroup *g;
	g = (struct Cgroup *)malloc(sizeof(struct Cgroup));
	g->path =  NULL;
	g->tasks = 0;
	g->cpu_fraction = 0;
	g->memory = 0;
	g->io_in_bps = 0;
	g->io_out_bps = 0;
	g->next = NULL;
	g->prev = NULL;
		
	fs = cg_enumerate_processes(controller, path);
	
	f = fopen(fs, "re");
	if (!f)
		return -1;
	
	while (cg_read_pid(f, &pid) == 0)
		n++;
	fclose(f);
	
	g->tasks = n;
	
	a = strlen(start);
	b = strlen(end);

	g->path = (char *)malloc(sizeof(char) * strlen(fs));
	memset(g->path, 0, strlen(g->path));

	
	if (!memcmp(start, fs, a)) {
		strncat(g->path, "/", 1);	
		if (!strcmp(end, fs+a-1))
			;
		else
			strncat(g->path, fs+a, strlen(fs)-a-b);
	}
	else
		strncat(g->path, fs, strlen(fs)-b);
	
	if (!strcmp(controller, "cpuacct")) {
		unsigned long long int new_usage, x, y;
		char *p, *v, *t = NULL;
		struct timespec ts;
		
		r = cg_get_path(controller, path, "cpuacct.usage", &p);
		if (r < 0)
			return -1;
		
		r = read_one_line_file(p, &v);
		free(p);
		if (r < 0)
			return r;		

		new_usage = strtoull(v, &t, 0);
		free(v);
		
		clock_gettime(CLOCK_MONOTONIC, &ts);
		
		x = ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec)-((uint64_t)old_time.tv_sec * 1000000000ULL + (uint64_t)old_time.tv_nsec);
		
		y = new_usage - old_usage;

		if (y > 0)
			g->cpu_fraction = (double)y / (double)x * 100;
		
	}


	else if (!strcmp(controller, "memory")) {
		char *p, *v, *x = NULL;
		r = cg_get_path(controller, path, "memory.usage_in_bytes", &p);
		if (r < 0)
			return -1;
		
		r = read_one_line_file(p, &v);
		free(p);
		if (r < 0)
			return -1;
		
		g->memory = strtoull(v, &x, 0);
		
		g->memory = g->memory / 1024 / 1024;		
		
		free(v);
	}
	
	else if (!strcmp(controller, "blkio")) {
		char *p;
		uint64_t wr = 0, rd = 0;
		struct timespec ts;
		uint64_t i, jr, jw;		

		r = cg_get_path(controller, path, "blkio.io_service_bytes", &p);
		if (r < 0)
			return -1;
		
		f = fopen(p, "re");
		free(p);
		
		if (!f) 
			return -1;
		
		for(;;) {
			char t[1024], *l, *x = NULL;
			uint64_t k, *q;
			
			if (!fgets(t, sizeof(t), f))
				break;
			
			l = strstrip(t);
			l += strcspn(l, " \t\n\r");
			l += strspn(l, " \t\n\r");

			if (first_word(l, "Read")) {
				l +=4;
				q = &rd;
			}
			else if(first_word(l, "Write")) {
				l += 5;
				q = &wr;
			}
			else
				continue;
			
			l += strspn(l, " \t\n\r");
			k = strtoull(l, &x, 0);
			*q += k;
		}

		fclose(f);

		i = ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec)-((uint64_t)old_time.tv_sec * 1000000000ULL + (uint64_t)old_time.tv_nsec);
		jr = rd - old_rd;
		jw = wr - old_wr;

		g->io_in_bps = (jr * 1000000000ULL) / i;
		g->io_out_bps = (jw * 1000000000ULL) / i;   
	}
	
	else {
		;
	}
	
	g->next = G->next;
	G->next = g;
	g->next->prev = g;
	g->prev = G;
	
}

static int cg_enumerate_subgroups(const char *controller, const char *path, DIR **_d) {
	char *fs = NULL;
	int r;
	DIR *d;

	r = cg_get_path(controller, path, "/", &fs);
	if (r < 0)
		return -1;

	d = opendir(fs);
	if (!d)
		return -1;

	*_d = d;
	return 0;
}



static int refresh_one(const char *controller, const char *path, unsigned iteration, unsigned depth) {
	int r;
	DIR *d = NULL;

	if (depth > 3)
		return 0;

	r = process(controller, path, iteration);
	if (r < 0)
		return -1;

	r = cg_enumerate_subgroups(controller, path, &d);
	if (r < 0)
		return -1;

	r = cg_read_subgroup(controller, path, iteration, depth, d);
	if (r < 0)
		return -1;


}

int cg_read_subgroup(const char *controller, const char *path, unsigned iteration, unsigned depth, DIR *d) {
	struct dirent *de;
	int r;
	
	for (;;) {
		char  *b;

		de = readdir(d);
		if (!de)
			return 0;
		
		if (de->d_type != DT_DIR) 
			continue;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) 
			continue;

		b = strdup(de->d_name);
		if (!b)
			return -1;

		fn = strjoin(path, "/", b, NULL);
		
		free(b);

		fn = path_kill_slashes(fn);

		r = refresh_one(controller, fn, iteration, depth+1);

		fn = NULL;
		
		if (r < 0)
			return -1;
		
	}

	return 0;
}

static int refresh(unsigned iteration) {
	int r;

	r = refresh_one("systemd", "/", iteration, 0);
	if (r < 0)
		return -1;
	r = refresh_one("cpuacct", "/", iteration, 0);
	if (r < 0)
		return -1;
	r = refresh_one("memory", "/", iteration, 0);
	if (r < 0)
		return -1;
	r = refresh_one("blkio", "/", iteration, 0);
	if (r < 0)
		return -1;
}


void display() {
	struct Cgroup *p;
	p = G->prev;
	
	printf("-----------------------path----------------------	-task-	  ----cpu(%)----	-memory(MB)-	-input(bp/s)-	-output(bp/s)-\n");
	
	while(p != G) {
		if (p->tasks != 0) {
			printf("%-50s	%4d", p->path, p->tasks);
			if (!p->cpu_fraction)
				printf("	%8s", "-");
			else
				printf("	%9.2lf", p->cpu_fraction);
			if (!p->memory)
				printf("	%8s", "-");
			else
				printf("	%5ld", p->memory);
			if (!p->io_in_bps)
                                printf("        %8s", "-");
                        else
                                printf("        %5ld", p->io_in_bps);
			if (!p->io_out_bps)
                                printf("        %8s", "-");
                        else
                                printf("        %5ld", p->io_out_bps);
			printf("\n");		
		}
		p = p->prev;
	}
}

int read_from_cpuacct(unsigned long long int *m) {
	FILE *f;
	char t[1024] = {0}, *c = NULL;
	char *x = NULL;
	unsigned long long l;
	
	f = fopen("/sys/fs/cgroup/cpuacct/cpuacct.usage", "re");

	if (!fgets(t, sizeof(t), f))
		return -1;

	c = strdup(t);
	c[strcspn(c, "\n\r")] = '\0';

	l = strtoull(c, &x, 0);
	
	*m = l;
	return 0;
}

int read_from_blkio(uint64_t *m, uint64_t *n) {

	uint64_t wr = 0, rd = 0;
	FILE *f;
	

	f = fopen("/sys/fs/cgroup/blkio/blkio.io_service_bytes", "re");
	if (!f) {
		printf("wrong\n");
		return -1;
	}	
	
	for(;;) {
        	char t[1024], *l, *x = NULL;
                uint64_t k, *q;

                if (!fgets(t, sizeof(t), f))
                	break;

               	l = strstrip(t);
                l += strcspn(l, " \t\n\r");
                l += strspn(l, " \t\n\r");

                if (first_word(l, "Read")) {
                	l +=4;
                        q = &rd;
                }
                else if(first_word(l, "Write")) {
                	l += 5;
                        q = &wr;
                }
                else
                	continue;

                l += strspn(l, " \t\n\r");
                k = strtoull(l, &x, 0);
                *q += k;
        }

        fclose(f);

}

int cgtop_main(int argc, char *argv[])
{
	
	unsigned iteration = 0;

	clock_gettime(CLOCK_MONOTONIC, &old_time);
	read_from_cpuacct(&old_usage);	
	read_from_blkio(&old_rd, &old_wr);	

	G = (struct Cgroup *)malloc(sizeof(struct Cgroup));
	G->next = G->prev = G;
	
	refresh(iteration++);
	
	display();
	
	return 0;
} 
