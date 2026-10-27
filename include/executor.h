#ifndef EXECUTOR_H
#define EXECUTOR_H
#include "terminator.h"
void notify_gui(void);
void add_line_locked(Tab *t, const char *s);
void append_text_locked(Tab *t, const char *text);
int write_full(int fd, const void *buffer, size_t len);
void *worker(void *arg);
void shell_process(int cmd_fd, int msg_fd);
void consume_messages(Tab *t);
#endif
