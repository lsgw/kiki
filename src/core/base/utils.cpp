#include "utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define BD_MAX_CLOSE   8192    /* Maximum file descriptors to close if sysconf(_SC_OPEN_MAX) is indeterminate */
#define BUF_SIZE       128
#define CPF_CLOEXEC    1

static int daemonFilePidFd = -1;

// 字符串分割函数  
std::vector<std::string> utils::split(const std::string& s, const std::string& pattern)
{
	int pos;
	std::vector<std::string> result;

	std::string str = s + pattern; //扩展字符串以方便操作
	int size=str.size();

	for (int i=0; i<size; i++) {
		pos=str.find(pattern,i);
		if (pos < size) {
			std::string s=str.substr(i,pos-i);
			result.push_back(s);
			i=pos+pattern.size()-1;
		}
	}
	return result;
}

int lockReg(int fd, int cmd, int type, int whence, int start, off_t len)
{
    struct flock fl;

    fl.l_type = type;
    fl.l_whence = whence;
    fl.l_start = start;
    fl.l_len = len;

    return fcntl(fd, cmd, &fl);
}

int lockRegion(int fd, int type, int whence, int start, int len)
{
    return lockReg(fd, F_SETLK, type, whence, start, len);
}

int alreadyRunning(const char* progName, const char* pidFile, int flags)
{
    int fd;
    char buf[BUF_SIZE];

    fd = open(pidFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        fprintf(stderr, "Could not open PID file %s\n", pidFile);
        return -1;
    }

    if (flags & CPF_CLOEXEC) {

        flags = fcntl(fd, F_GETFD);                     /* Fetch flags */
        flags |= FD_CLOEXEC;                            /* Turn on FD_CLOEXEC */
        fcntl(fd, F_SETFD, flags);
    }

    if (lockRegion(fd, F_WRLCK, SEEK_SET, 0, 0) == -1) {
        if (errno  == EAGAIN || errno == EACCES) {
            fprintf(stderr, "PID file '%s' is locked; probably '%s' is already running\n", pidFile, progName);
        } else {
            fprintf(stderr, "Unable to lock PID file '%s'\n", pidFile);
        }
        close(fd);
        return -2;
    }

    if (ftruncate(fd, 0) == -1) {
        fprintf(stderr, "Could not truncate PID file '%s'\n", pidFile);
        close(fd);
        return -3;
    }

    snprintf(buf, BUF_SIZE, "%ld\n", (long) getpid());
    if (write(fd, buf, strlen(buf)) != static_cast<ssize_t>(strlen(buf))) {
        fprintf(stderr, "Writing to PID file '%s'\n", pidFile);
        close(fd);
        return -4;
    }

    return fd;
}

int utils::becomeDaemon()
{
    switch (fork()) {                      
    case -1: return -1;
    case 0:  break;                     
    default: _exit(EXIT_SUCCESS);        
    }

    if (setsid() == -1)                     
        return -1;

    switch (fork()) {                  
    case -1: return -1;
    case 0:  break;
    default: _exit(EXIT_SUCCESS);
    }

    umask(0);                          
    //chdir("/");                       

    int maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd == -1) {                 
        maxfd = BD_MAX_CLOSE;
    }
    for (int fd=0; fd<maxfd; fd++) {
        close(fd);
    }
    
    close(STDIN_FILENO);          
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDIN_FILENO) {
        return -1;
    }
    if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO) {
        return -1;
    }
    if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO) {
        return -1;
    }

    daemonFilePidFd = alreadyRunning("xgnet", "xgnet.pid", 1);
    if (daemonFilePidFd < 0) {
        return -1;
    }

    return 0;
}

