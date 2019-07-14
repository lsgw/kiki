#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{

	fprintf(stderr, "hello sum\n");
    struct dirent* dp = NULL;

    DIR* dirp = opendir("/dev/fd");

    while (1) {
        errno = 0;
        dp = readdir(dirp);
        if (dp == NULL) {
            break;
        }
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        fprintf(stderr, "%s/%s\n", "dev/fd", dp->d_name);
    }
    fprintf(stderr, "DIR self: %d\n", dirfd(dirp));
    if (errno != 0) {
        printf("error\n");
    }
    if (closedir(dirp) == -1) {
        printf("closedir error\n");
    }


    write(1, "hello\n", 5);

    int i = 0;
    while (i < 30) {
        fprintf(stderr, "i = %d\n", i);
        sleep(1);
        i++;
    }
}
