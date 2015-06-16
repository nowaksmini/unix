#ifndef _LAB_H
#define _LAB_H

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/stat.h>

#define ERR(src)    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),\
                    perror(src), kill(0, SIGKILL), exit(EXIT_FAILURE))

# define TEMP_FAILURE_RETRY(expression) \
  (__extension__\
    ({ long int __result;\
       do __result = (long int) (expression); \
       while (__result == -1L && errno == EINTR); \
       __result; }))

# define SAFE_FUNCTION(expression, name) \
    (__extension__\
    ({if (-1 == (int)(expression))\
    ERR(name);}))

#define MAX_MSG_SIZE 256

volatile sig_atomic_t _sigNo;

void sigchld_handler(int);
void secure_sleep(int);
sigset_t set_signals(int, sigset_t*, int, ...);
void set_signal_handler(void (*)(int), int);
void set_signal_action(void (*)(int, siginfo_t *, void *), int);
void set_default_handlers(int, ...);
void send_signal(pid_t, int);
void enqueue_signal(pid_t, int, int);
void pprintf(const char*, ...);
ssize_t bulk_read(int, char*, size_t);
ssize_t bulk_write(int, char*, size_t);
void redirect_stream(int, int);
void set_write_lock(int, int, int, int);
void read_dir(char*, void (*)(struct dirent*));
int open_file(char*, int, int);
void close_fd(int);
FILE* fopen_file(char*, char*);
void fclose_file(FILE*);
void* safe_malloc(size_t);
void process_dir_entry(const struct dirent*, struct stat*);
void make_dir(const char*, mode_t);
void change_dir(char*);
void make_fifo(char*, int);
void unlink_fifo(char*);
void create_pipe(int**);
static inline void set_timer(int seconds, int microsec)
{
    struct itimerval ts;
    memset(&ts, 0, sizeof(struct itimerval));
    ts.it_value.tv_sec = seconds;
    ts.it_value.tv_usec = microsec;
    setitimer(ITIMER_PROF, &ts, NULL);
}

#endif
