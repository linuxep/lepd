/*
 * running on server to emulate cpu load
 *
 * Copyright (c) 2016, Barry Song <21cnbao@gmail.com> 
 *
 * Licensed under GPLv2 or later.
 */


#include <stdlib.h>
void main(void)
{
	fork();
	while(1){
		volatile int i;
		usleep(50 * (random()%10));
		for(i=0;i<100000;i++);
	}
}
