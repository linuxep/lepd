/*
 * running on server to emulate malloc/free load
 *
 * Copyright (c) 2016, Barry Song <21cnbao@gmail.com> 
 *
 * Licensed under GPLv2 or later.
 */

void main(void)
{
	while(1){
		int *p=malloc(200*1024*1024);
		int i;
		for (i=0;i<50*1024*1024;i++) {
			p[i]=0;
			if (i%(1024*1024) == 0)
				sleep(0.1);
		}
		free(p);
	}
}
