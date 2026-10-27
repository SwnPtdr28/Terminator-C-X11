#ifndef TERMINATOR_H
#define TERMINATOR_H

#include <pthread.h>
#include <sys/types.h>
#include <stddef.h>
#include <limits.h>
#include <signal.h>

#define MAX_TABS 8
#define MAX_LINES 600
#define LINE_LEN 512
#define INPUT_LEN 1024
#define LINE_HEIGHT 18

typedef struct {
    pthread_t thread;
    pid_t shell_pid;
    int cmd_write;
    int msg_read;
    unsigned char msg_buf[4096];
    size_t msg_used;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    int stop;
    int pending;
    char command[INPUT_LEN];
    char continuation[INPUT_LEN];
    char input[INPUT_LEN];
    int input_len;
    int cursor;
    int history_pos;
    char lines[MAX_LINES][LINE_LEN];
    int line_count;
    int scroll;
    int running;
    pid_t child_pid;
    int output_read;
    int output_write;
    int exit_status;
    volatile sig_atomic_t cancel;
    char cwd[PATH_MAX]; /* Independent current directory for this tab */
} Tab;

#endif
