#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
    for (int i=0; i<argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    printf("-------------------------\n");
	
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

        fprintf(stdout, "%s/%s\n", "dev/fd", dp->d_name);
    }
    fprintf(stdout, "DIR self: %d\n", dirfd(dirp));
    if (errno != 0) {
        fprintf(stdout, "error\n");
    }
    if (closedir(dirp) == -1) {
        fprintf(stdout,"closedir error\n");
    }
}
