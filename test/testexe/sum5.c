#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
    for (int i=0; i<argc; i++) {
        fprintf(stderr, "argv[%d] = %s\n", i, argv[i]);
    }
	char buf[1024] = { '\0' };
    int i = 0;
    while (i < 30) {
    	sprintf(buf, "hello %d", i);
    	write(6, buf, strlen(buf));
        
        fprintf(stderr, "i = %d\n", i);
        sleep(1);
        i++;
    }

	return 0;
}