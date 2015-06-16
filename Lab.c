#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "Lab.h"

volatile sig_atomic_t _sigNo;

void default_sig_handler(int signo)
{
    _sigNo = signo;
}

void sigchld_handler(int sig)
{
    pid_t pid;

    for(;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if(0 == pid) break;
        if(0 > pid)
        {
            if(ECHILD == errno) break;
            ERR("waitpid");
        }
    }
}

void secure_sleep(int duration)
{
    struct timespec tt, t;
    t.tv_sec = 0;
    t.tv_nsec = duration;

    for(tt = t; nanosleep(&tt, &tt);)
    {
        if(EINTR != errno) ERR("nanosleep");
    }
}

sigset_t set_signals(int behaviour, sigset_t* set, int num, ...)
{
    int i;
    sigset_t oldmask;
    va_list arguments;
    va_start(arguments, num);

    sigemptyset(set);

    for(i = 0; i < num; i++)
    {
        sigaddset(set, (int)va_arg(arguments, int));
    }
    va_end(arguments);

    sigprocmask(behaviour, set, &oldmask);

    return oldmask;
}

void set_signal_handler(void (*func)(int), int signo)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = func;
    if(-1 == sigaction(signo, &action, NULL)) ERR("sigaction");
}

void set_signal_action(void (*func)(int, siginfo_t *, void *), int signo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = func;
    act.sa_flags = SA_SIGINFO;
    if(-1 == sigaction(signo, &act, NULL)) ERR("sigaction");
}

void set_default_handlers(int num, ...)
{
    int i;
    va_list arguments;
    va_start(arguments, num);

    for(i = 0; i < num; i++)
    {
        set_signal_handler(default_sig_handler,
                            (int)va_arg(arguments, int));
    }
    va_end(arguments);
}

void send_signal(pid_t pid, int signo)
{
    fprintf(stderr, "[%d] Sending signal %d to pid: %d\n", getpid(), signo, pid);
    if(TEMP_FAILURE_RETRY(fflush(stdout))) ERR("fflush");
    if(-1 == kill(pid, signo)) ERR("kill");
}

void enqueue_signal(pid_t pid, int signo, int value)
{
    union sigval val;
    val.sival_int = value;

    fprintf(stderr, "[%d] Sending signal %d to pid: %d\n", getpid(), signo, pid);
    if(TEMP_FAILURE_RETRY(fflush(stdout))) ERR("fflush");

    do
    {
        if(-1 != sigqueue(pid, signo, val)) break;
    } while(errno != EAGAIN);
}

void pprintf(const char* string, ...)
{
    char buf[MAX_MSG_SIZE] = "";

    va_list arguments;
    va_start(arguments, string);
    vsnprintf(buf, MAX_MSG_SIZE, string, arguments);
    va_end(arguments);

    printf("%s", buf);
    if(TEMP_FAILURE_RETRY(fflush(stdout))) ERR("fflush");
}

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    int c;
    size_t len;

    len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if(c < 0) ERR("read");
        if(0 == c) break;   /* in case of EOF */
        buf += c;
        len += c;
        count -= c;
    } while(count > 0);

    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    int c;
    size_t len;

    len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if(c < 0) ERR("write");
        buf += c;
        len += c;
        count -= c;
    } while(count > 0);

    return len;
}

void redirect_stream(int from_fd, int to_fd)
{
    if(-1 == TEMP_FAILURE_RETRY(dup2(to_fd, from_fd))) ERR("dup2");
}

void set_write_lock(int fd, int start, int len, int mode)
{
    struct flock l;
    l.l_whence = SEEK_SET;
    l.l_start = start;
    l.l_len = len;
    l.l_type = mode;

    if(-1 == TEMP_FAILURE_RETRY(fcntl(fd, F_SETLKW, &l))) ERR("fcntl");
}

void read_dir(char* path, void (*f)(struct dirent*))
{
    DIR* dirp;
    struct dirent* dp;

    if(NULL == (dirp = opendir(path))) ERR("opendir");

    do {
        errno = 0;
        if(NULL != (dp = readdir(dirp)))
        {
            f(dp);
        }
    } while(dp != NULL);

    if(errno != 0) ERR("readdir");
    TEMP_FAILURE_RETRY(closedir(dirp));
}

int open_file(char* path, int flag, int mode)
{
    int fd;
    if(-1 == (fd = TEMP_FAILURE_RETRY(open(path, flag, mode)))) ERR("open");
    return fd;
}

void close_fd(int fd)
{
    if(-1 == TEMP_FAILURE_RETRY(close(fd))) ERR("close");
}

FILE* fopen_file(char* path, char* flags)
{
    FILE* file;
    if((file = (FILE*)TEMP_FAILURE_RETRY(fopen(path, flags))) == NULL) ERR("fopen");
    return file;
}

void fclose_file(FILE* file)
{
    if(-1 == TEMP_FAILURE_RETRY(fclose(file))) ERR("fclose");
}

void* safe_malloc(size_t size)
{
    void* value;
    if(NULL == (value = malloc(size))) ERR("malloc");
    return value;
}

void process_dir_entry(const struct dirent* entry, struct stat* file_stat)
{
    if(-1 == lstat(entry->d_name, file_stat)) ERR("lstat");
}

void make_dir(const char* path, mode_t mode)
{
    if(-1 == mkdir(path, mode)) ERR("mkdir");
}

void change_dir(char* path)
{
    if(-1 == chdir(path)) ERR("chdir");
}

void make_fifo(char* path, int flags)
{
    if(-1 == flags) flags = O_EXCL | O_CREAT | S_IRWXU | S_IRWXG | S_IRWXO;
    if(mkfifo(path, flags) < 0) ERR("mkfifo");
}

void unlink_fifo(char* path)
{
    if(unlink(path) < 0) ERR("unlink");
}

void create_pipe(int* pipe_fd[2])
{
    if(-1 == pipe(*pipe_fd)) ERR("pipe");
}
