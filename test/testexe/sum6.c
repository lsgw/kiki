#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#define OUTFD 6

int main(int argc, char* argv[])
{
	char line[1024] = { '\0' };
    int n = 0;
    int i = 0;
    int j = 0;
    int t = 0;

    while ((n = read(OUTFD, line, 1024)) > 0) {
        line[n] = '\0';
        sscanf(line, "%d%d", &i, &j);
        t = i + j;

        sprintf(line, "%2d + %2d = %2d", i, j, t);
        write(OUTFD, line, strlen(line));
    }

	return 0;
}