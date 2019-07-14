#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
	char buf[1024] = { '\0' };

	read(0, buf, 1024);
	fprintf(stderr, "sum read = %s\n", buf);
	sleep(5);

    int i = 0;
    while (i < 30) {
        fprintf(stderr, "i = %d\n", i);
        sleep(1);
        i++;
    }

	return 0;
}