#define _POSIX_C_SOURCE 200809L
#include "tabs.h"
#include "executor.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

int process_mode = 0;
Tab *tabs[MAX_TABS];
int tab_count = 0;
int active = 0;
int wake_pipe[2] = {-1, -1};

int create_tab(void) {
    if (tab_count >= MAX_TABS) return 0;
    Tab *t = calloc(1, sizeof(*t));
    if (!t) return 0;
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->ready, NULL);
    t->line_count = 1;
    snprintf(t->lines[0], LINE_LEN, "Terminator tab %d ready", tab_count + 1);
    t->msg_read = -1;
    t->cmd_write = -1;

    if (!getcwd(t->cwd, sizeof(t->cwd))) {
        perror("getcwd");
        pthread_cond_destroy(&t->ready);
        pthread_mutex_destroy(&t->lock);
        free(t);
        return 0;
    }

    if (process_mode) {
        int to_child[2], from_child[2];
        if (pipe(to_child) < 0) {
            pthread_cond_destroy(&t->ready);
            pthread_mutex_destroy(&t->lock);
            free(t);
            return 0;
        }
        if (pipe(from_child) < 0) {
            close(to_child[0]); close(to_child[1]);
            pthread_cond_destroy(&t->ready);
            pthread_mutex_destroy(&t->lock);
            free(t);
            return 0;
        }
        pid_t pid = fork();
        if (pid < 0) {
            close(to_child[0]); close(to_child[1]);
            close(from_child[0]); close(from_child[1]);
            pthread_cond_destroy(&t->ready);
            pthread_mutex_destroy(&t->lock);
            free(t);
            return 0;
        }
        if (pid == 0) {
            close(to_child[1]); close(from_child[0]);
            for (int i = 0; i < tab_count; i++) {
                if (tabs[i]->cmd_write >= 0) close(tabs[i]->cmd_write);
                if (tabs[i]->msg_read >= 0) close(tabs[i]->msg_read);
            }
            if (wake_pipe[0] >= 0) close(wake_pipe[0]);
            if (wake_pipe[1] >= 0) close(wake_pipe[1]);
            shell_process(to_child[0], from_child[1]);
        }
        close(to_child[0]); close(from_child[1]);
        t->shell_pid = pid;
        t->cmd_write = to_child[1];
        t->msg_read = from_child[0];
        int flags = fcntl(t->msg_read, F_GETFL, 0);
        if (flags >= 0) fcntl(t->msg_read, F_SETFL, flags | O_NONBLOCK);
    } else if (pthread_create(&t->thread, NULL, worker, t) != 0) {
        pthread_cond_destroy(&t->ready);
        pthread_mutex_destroy(&t->lock);
        free(t);
        return 0;
    }
    tabs[tab_count++] = t;
    active = tab_count - 1;
    return 1;
}

void destroy_tab(Tab *t) {
    pthread_mutex_lock(&t->lock);
    t->stop = 1;
    pid_t pid = t->child_pid;
    pthread_cond_signal(&t->ready);
    pthread_mutex_unlock(&t->lock);
    if (process_mode) {
        if (t->cmd_write >= 0) close(t->cmd_write);
        if (t->shell_pid > 0) {
            kill(t->shell_pid, SIGTERM);
            waitpid(t->shell_pid, NULL, 0);
        }
        if (t->msg_read >= 0) close(t->msg_read);
    } else {
        if (pid > 0) kill(pid, SIGTERM);
        pthread_join(t->thread, NULL);
    }
    pthread_cond_destroy(&t->ready);
    pthread_mutex_destroy(&t->lock);
    free(t);
}

void close_active_tab(void) {
    if (tab_count <= 0) return;

    Tab *t = tabs[active];
    for (int i = active; i < tab_count - 1; i++)
        tabs[i] = tabs[i + 1];

    tab_count--;
    if (tab_count > 0) {
        if (active >= tab_count)
            active = tab_count - 1;
    } else {
        active = 0;
    }

    destroy_tab(t);
    /* Do not create a replacement when the last tab closes. */
}
