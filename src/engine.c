
#define _POSIX_C_SOURCE 200809L

#include "engine.h"
#include "shell.h"
#include "history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXTOK 160
#define MAXCMD 20
#define MAXARGS 80
#define TOKLEN 1024

typedef struct {
    char *argv[MAXARGS];
    int argc;
    char *in, *out, *err;
    int append;
    int err_append;
} Cmd;

typedef struct {
    Cmd cmds[MAXCMD];
    int n;
    int background;
} Pipeline;

static void emit_error(EngineEmit emit, void *ctx, const char *msg)
{
    emit(ctx, msg);
    emit(ctx, "\n");
}

static int operator(char c)
{
    return c == '|' || c == '<' || c == '>' ||
           c == '&' || c == ';';
}

static void free_tokens(char **tok, int n)
{
    for (int i = 0; i < n; i++)
        free(tok[i]);
}

/* ---------- Command substitution ---------- */

typedef struct {
    char output[TOKLEN];
    size_t used;
} SubOutput;

static void sub_emit(void *ctx, const char *s)
{
    SubOutput *o = ctx;

    while (*s && o->used < sizeof(o->output) - 1)
        o->output[o->used++] = *s++;

    o->output[o->used] = '\0';
}

static int run_substitution(
    Engine *e,
    const char *command,
    char *result,
    size_t result_size)
{
    Engine *sub = malloc(sizeof(*sub));

    if (!sub)
        return -1;

    *sub = *e;

    volatile sig_atomic_t local_cancel = 0;
    volatile pid_t current = 0;

    sub->cancel = &local_cancel;

    SubOutput output = {0};

    engine_run(sub, command, sub_emit, &output, &current);

    while (output.used > 0 &&
           output.output[output.used - 1] == '\n')
    {
        output.output[--output.used] = '\0';
    }

    snprintf(result, result_size, "%s", output.output);

    free(sub);
    return 0;
}

/* ---------- Tokenizer ---------- */

static int tokenize(const char *src, char **tok, int *nt, Engine *e)
{
    *nt = 0;
    const char *p = src;

    while (*p)
    {
        while (isspace((unsigned char)*p))
            p++;

        if (!*p || *p == '#')
            break;

        if (*nt >= MAXTOK - 1)
            return -1;

        char out[TOKLEN];
        int len = 0;
        int quote = 0;
        int started = 0;

        /*
         * Recognize stderr redirection operators:
         * 2> and 2>>
         */
        if (p[0] == '2' && p[1] == '>')
        {
            out[len++] = *p++;
            out[len++] = *p++;

            if (*p == '>')
                out[len++] = *p++;
        }
        else if (operator(*p))
        {
            char c = *p++;
            out[len++] = c;

            if (c == '>' && *p == '>')
                out[len++] = *p++;
        }
        else
        {
            while (*p)
            {
                char c = *p;

                if (!quote &&
                    (isspace((unsigned char)c) ||
                     operator(c) ||
                     (c == '2' && p[1] == '>' && len == 0)))
                    break;

                if (c == '\\' && p[1] && quote != '\'')
                {
                    p++;
                    c = *p++;
                }
                else if (c == '\'' || c == '"')
                {
                    if (!quote)
                    {
                        quote = c;
                        p++;
                        started = 1;
                        continue;
                    }

                    if (quote == c)
                    {
                        quote = 0;
                        p++;
                        continue;
                    }

                    p++;
                }
                else
                {
                    p++;
                }

                started = 1;

                if (c == '$' && quote != '\'')
                {
                    if (*p == '(')
                    {
                        p++;

                        const char *start = p;
                        int depth = 1;
                        int inner_quote = 0;

                        while (*p && depth > 0)
                        {
                            if (*p == '\\' && p[1])
                            {
                                p += 2;
                                continue;
                            }

                            if (*p == '\'' || *p == '"')
                            {
                                if (!inner_quote)
                                    inner_quote = *p;
                                else if (inner_quote == *p)
                                    inner_quote = 0;
                            }
                            else if (!inner_quote)
                            {
                                if (*p == '(')
                                    depth++;
                                else if (*p == ')')
                                    depth--;
                            }

                            if (depth > 0)
                                p++;
                        }

                        if (depth != 0)
                            return -1;

                        size_t command_len = (size_t)(p - start);

                        if (command_len >= TOKLEN)
                            return -1;

                        char command[TOKLEN];

                        memcpy(command, start, command_len);
                        command[command_len] = '\0';

                        p++;

                        char result[TOKLEN];

                        if (run_substitution(
                                e, command, result,
                                sizeof(result)) < 0)
                            return -1;

                        for (size_t j = 0;
                             result[j] && len < TOKLEN - 1;
                             j++)
                            out[len++] = result[j];

                        continue;
                    }

                    char name[128];
                    int z = 0;

                    if (*p == '?')
                    {
                        p++;
                        snprintf(name, sizeof(name),
                                 "%d", e->last_status);
                    }
                    else if (*p == '$')
                    {
                        p++;
                        snprintf(name, sizeof(name),
                                 "%ld", (long)getpid());
                    }
                    else
                    {
                        while ((isalnum((unsigned char)*p) ||
                                *p == '_') && z < 127)
                            name[z++] = *p++;

                        name[z] = '\0';

                        if (z)
                        {
                            const char *v = getenv(name);
                            snprintf(name, sizeof(name),
                                     "%s", v ? v : "");
                        }
                        else
                            snprintf(name, sizeof(name), "$");
                    }

                    for (int j = 0;
                         name[j] && len < TOKLEN - 1;
                         j++)
                        out[len++] = name[j];
                }
                else if (len < TOKLEN - 1)
                {
                    out[len++] = c;
                }
            }

            if (quote)
                return -1;

            if (!started)
                continue;
        }

        out[len] = '\0';
        tok[*nt] = strdup(out);

        if (!tok[*nt])
            return -1;

        (*nt)++;
    }

    return 0;
}

/* ---------- Parser ---------- */

static int parse(char **tok, int n, Pipeline *p)
{
    memset(p, 0, sizeof(*p));
    p->n = 1;

    for (int i = 0; i < n; i++)
    {
        Cmd *c = &p->cmds[p->n - 1];

        if (!strcmp(tok[i], "|"))
        {
            if (!c->argc || p->n >= MAXCMD)
                return -1;

            p->n++;
        }
        else if (!strcmp(tok[i], "<") ||
                 !strcmp(tok[i], ">") ||
                 !strcmp(tok[i], ">>") ||
                 !strcmp(tok[i], "2>") ||
                 !strcmp(tok[i], "2>>"))
        {
            if (i + 1 >= n)
                return -1;

            char *op = tok[i];
            char *filename = tok[++i];

            if (!strcmp(op, "<"))
            {
                c->in = filename;
            }
            else if (!strcmp(op, ">") ||
                     !strcmp(op, ">>"))
            {
                c->out = filename;
                c->append = !strcmp(op, ">>");
            }
            else
            {
                c->err = filename;
                c->err_append = !strcmp(op, "2>>");
            }
        }
        else if (!strcmp(tok[i], "&"))
        {
            if (i != n - 1)
                return -1;

            p->background = 1;
        }
        else if (!strcmp(tok[i], ";"))
        {
            return -1;
        }
        else
        {
            if (c->argc >= MAXARGS - 1)
                return -1;

            c->argv[c->argc++] = tok[i];
            c->argv[c->argc] = NULL;
        }
    }

    for (int i = 0; i < p->n; i++)
        if (!p->cmds[i].argc)
            return -1;

    return 0;
}

/* ---------- Job management ---------- */

static void reap_jobs(Engine *e)
{
    for (int i = 0; i < ENGINE_JOBS; i++)
    {
        if (e->jobs[i].pid > 0 &&
            e->jobs[i].state == 1)
        {
            int w;
            pid_t r = waitpid(
                e->jobs[i].pid, &w,
                WNOHANG | WUNTRACED | WCONTINUED);

            if (r > 0)
            {
                if (WIFEXITED(w) || WIFSIGNALED(w))
                    e->jobs[i].state = 3;
                else if (WIFSTOPPED(w))
                    e->jobs[i].state = 2;
                else if (WIFCONTINUED(w))
                    e->jobs[i].state = 1;
            }
        }
    }
}

static int add_job(
    Engine *e, pid_t pid,
    const char *s, int state)
{
    for (int i = 0; i < ENGINE_JOBS; i++)
    {
        if (!e->jobs[i].pid || e->jobs[i].state == 3)
        {
            e->jobs[i].pid = pid;
            e->jobs[i].state = state;

            snprintf(e->jobs[i].command,
                     sizeof(e->jobs[i].command),
                     "%.255s", s);

            return i;
        }
    }

    return -1;
}

/* ---------- Source command ---------- */

static int source_file(
    Engine *e,
    const char *filename,
    EngineEmit emit,
    void *ctx,
    volatile pid_t *current)
{
    char path[PATH_MAX];

    if (filename[0] == '/')
        snprintf(path, sizeof(path), "%s", filename);
    else
        snprintf(path, sizeof(path),
                 "%s/%s", e->cwd, filename);

    FILE *fp = fopen(path, "r");

    if (!fp)
    {
        char error[PATH_MAX + 128];

        snprintf(error, sizeof(error),
                 "source: %s: %s\n",
                 filename, strerror(errno));

        emit(ctx, error);
        return 1;
    }

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    int status = 0;

    while ((length = getline(&line, &capacity, fp)) != -1)
    {
        while (length > 0 &&
               (line[length - 1] == '\n' ||
                line[length - 1] == '\r'))
            line[--length] = '\0';

        if (length == 0)
            continue;

        status = engine_run(
            e, line, emit, ctx, current);
    }

    if (ferror(fp))
    {
        emit(ctx, "source: error while reading file\n");
        status = 1;
    }

    free(line);
    fclose(fp);

    return status;
}

/* ---------- Built-in commands ---------- */

static int built_in(
    Engine *e,
    Cmd *c,
    EngineEmit emit,
    void *ctx,
    volatile pid_t *current)
{
    const char *name = c->argv[0];
    char buf[2048];

    if (!strcmp(name, "cd"))
    {
        char err[512] = "";

        int r = shell_cd(
            e->cwd, c->argc, c->argv,
            err, sizeof(err));

        if (r)
            emit(ctx, err);

        return r;
    }

    if (!strcmp(name, "exit"))
    {
        emit(ctx, "Use Ctrl+Shift+W to close this tab.\n");
        return 0;
    }

    if (!strcmp(name, "source") ||
        !strcmp(name, "."))
    {
        if (c->argc < 2)
        {
            emit(ctx, "source: filename required\n");
            return 2;
        }

        return source_file(
            e, c->argv[1], emit, ctx, current);
    }

    if (!strcmp(name, "history"))
    {
        history_print(emit, ctx);
        return 0;
    }

    if (!strcmp(name, "jobs"))
    {
        reap_jobs(e);

        for (int i = 0; i < ENGINE_JOBS; i++)
        {
            if (e->jobs[i].pid)
            {
                snprintf(buf, sizeof(buf),
                         "[%d] %ld %s %s\n",
                         i + 1,
                         (long)e->jobs[i].pid,
                         e->jobs[i].state == 1 ? "Running" :
                         e->jobs[i].state == 2 ? "Stopped" :
                                                  "Done",
                         e->jobs[i].command);

                emit(ctx, buf);
            }
        }

        return 0;
    }

    if (!strcmp(name, "fg") ||
        !strcmp(name, "bg"))
    {
        reap_jobs(e);

        int idx = -1;

        if (c->argc > 1)
        {
            const char *arg = c->argv[1];

            if (*arg == '%')
                arg++;

            idx = atoi(arg);

            if (idx > 0)
                idx--;
        }
        else
        {
            for (int i = ENGINE_JOBS - 1; i >= 0; i--)
            {
                if (e->jobs[i].pid &&
                    e->jobs[i].state != 3)
                {
                    idx = i;
                    break;
                }
            }
        }

        if (idx < 0 || idx >= ENGINE_JOBS ||
            !e->jobs[idx].pid ||
            e->jobs[idx].state == 3)
        {
            emit_error(emit, ctx, "No such active job");
            return 1;
        }

        pid_t pid = e->jobs[idx].pid;

        kill(-pid, SIGCONT);
        e->jobs[idx].state = 1;

        if (!strcmp(name, "bg"))
            return 0;

        if (current)
            *current = pid;

        int w = 0;
        pid_t r;

        do {
            r = waitpid(pid, &w, WUNTRACED);
        } while (r < 0 && errno == EINTR);

        if (current)
            *current = 0;

        if (r < 0)
            return 1;

        if (WIFSTOPPED(w))
        {
            e->jobs[idx].state = 2;
            return 128 + WSTOPSIG(w);
        }

        e->jobs[idx].state = 3;

        return WIFEXITED(w)
                   ? WEXITSTATUS(w)
                   : 128 + WTERMSIG(w);
    }

    return -999;
}

/* ---------- Pipeline execution ---------- */

static int execute_pipeline(
    Engine *e,
    Pipeline *p,
    const char *original,
    EngineEmit emit,
    void *ctx,
    volatile pid_t *current)
{
    if (p->n == 1 && !p->background)
    {
        int r = built_in(
            e, &p->cmds[0], emit, ctx, current);

        if (r != -999)
            return r;
    }

    int capture[2];

    if (pipe(capture) < 0)
    {
        emit_error(emit, ctx, "capture pipe failed");
        return 1;
    }

    int prev = -1;
    pid_t pids[MAXCMD] = {0};
    pid_t group = 0;
    int started = 0;

    for (int i = 0; i < p->n; i++)
    {
        int next[2] = {-1, -1};

        if (i < p->n - 1 && pipe(next) < 0)
        {
            emit_error(emit, ctx, "pipeline pipe failed");
            break;
        }

        pid_t pid = fork();

        if (pid == 0)
        {
            if (group)
                setpgid(0, group);
            else
                setpgid(0, 0);

            if (prev >= 0)
                dup2(prev, STDIN_FILENO);

            if (next[1] >= 0)
                dup2(next[1], STDOUT_FILENO);
            else
                dup2(capture[1], STDOUT_FILENO);

            dup2(capture[1], STDERR_FILENO);

            if (prev >= 0)
                close(prev);

            if (next[0] >= 0)
                close(next[0]);

            if (next[1] >= 0)
                close(next[1]);

            close(capture[0]);
            close(capture[1]);

            if (chdir(e->cwd) < 0)
            {
                perror("chdir");
                _exit(1);
            }

            Cmd *c = &p->cmds[i];

            /* Input redirection */
            if (c->in)
            {
                int fd = open(c->in, O_RDONLY);

                if (fd < 0)
                {
                    perror(c->in);
                    _exit(1);
                }

                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            /* Standard output redirection */
            if (c->out)
            {
                int fd = open(
                    c->out,
                    O_WRONLY | O_CREAT |
                    (c->append ? O_APPEND : O_TRUNC),
                    0666);

                if (fd < 0)
                {
                    perror(c->out);
                    _exit(1);
                }

                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            /*
             * Standard error redirection.
             * This is the Task 4 fix.
             */
            if (c->err)
            {
                int fd = open(
                    c->err,
                    O_WRONLY | O_CREAT |
                    (c->err_append ? O_APPEND : O_TRUNC),
                    0666);

                if (fd < 0)
                {
                    perror(c->err);
                    _exit(1);
                }

                if (dup2(fd, STDERR_FILENO) < 0)
                {
                    perror("dup2 stderr");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            if (!strcmp(c->argv[0], "cd") ||
                !strcmp(c->argv[0], "history"))
            {
                dprintf(STDERR_FILENO,
                        "Built-in not supported in a pipeline\n");
                _exit(2);
            }

            execvp(c->argv[0], c->argv);

            int code = errno == ENOENT ? 127 : 126;
            perror(c->argv[0]);
            _exit(code);
        }

        if (pid < 0)
        {
            emit_error(emit, ctx, "fork failed");

            if (next[0] >= 0)
                close(next[0]);

            if (next[1] >= 0)
                close(next[1]);

            break;
        }

        if (!group)
            group = pid;

        setpgid(pid, group);
        pids[started++] = pid;

        if (prev >= 0)
            close(prev);

        if (next[1] >= 0)
            close(next[1]);

        prev = next[0];
    }

    if (prev >= 0)
        close(prev);

    close(capture[1]);

    if (p->background)
    {
        int j = add_job(e, group, original, 1);
        char buf[128];

        snprintf(buf, sizeof(buf),
                 "[%d] %ld started\n",
                 j + 1, (long)group);

        emit(ctx, buf);

        int fl = fcntl(capture[0], F_GETFL);
        fcntl(capture[0], F_SETFL, fl | O_NONBLOCK);
    }

    if (current && !p->background)
        *current = group;

    int result = 0;
    char chunk[1024];
    ssize_t n;

    if (!p->background)
    {
        int fl = fcntl(capture[0], F_GETFL);
        fcntl(capture[0], F_SETFL, fl | O_NONBLOCK);

        int remaining = started;
        int status[MAXCMD] = {0};
        int seen[MAXCMD] = {0};

        while (remaining > 0)
        {
            struct pollfd fd = {
                capture[0], POLLIN | POLLHUP, 0
            };

            poll(&fd, 1, 80);

            while ((n = read(capture[0],
                             chunk,
                             sizeof(chunk) - 1)) > 0)
            {
                chunk[n] = '\0';
                emit(ctx, chunk);
            }

            for (int j = 0; j < started; j++)
            {
                if (!seen[j])
                {
                    int w;

                    pid_t r = waitpid(
                        pids[j], &w,
                        WNOHANG | WUNTRACED);

                    if (r > 0)
                    {
                        seen[j] = 1;
                        remaining--;
                        status[j] = w;

                        if (WIFSTOPPED(w))
                            add_job(e, group, original, 2);
                    }
                    else if (r < 0 && errno == ECHILD)
                    {
                        seen[j] = 1;
                        remaining--;
                    }
                }
            }
        }

        if (started)
        {
            int w = status[started - 1];

            if (WIFSTOPPED(w))
                result = 128 + WSTOPSIG(w);
            else if (WIFEXITED(w))
                result = WEXITSTATUS(w);
            else if (WIFSIGNALED(w))
                result = 128 + WTERMSIG(w);
        }
    }
    else
    {
        while ((n = read(capture[0],
                         chunk,
                         sizeof(chunk) - 1)) > 0)
        {
            chunk[n] = '\0';
            emit(ctx, chunk);
        }
    }

    close(capture[0]);

    if (current)
        *current = 0;

    return result;
}

/* ---------- multiWatch ---------- */

static int multiwatch(
    Engine *e,
    const char *input,
    EngineEmit emit,
    void *ctx,
    volatile pid_t *current)
{
    const char *p = strchr(input, '[');

    if (!p)
    {
        emit_error(
            emit, ctx,
            "Usage: multiWatch [\"cmd1\", \"cmd2\"]");
        return 2;
    }

    char *commands[8] = {0};
    int count = 0;

    while (*p && *p != ']')
    {
        while (*p && *p != '"' && *p != ']')
            p++;

        if (*p != '"')
            break;

        p++;
        const char *start = p;

        while (*p && *p != '"')
            p++;

        if (!*p || count >= 8)
            break;

        commands[count++] =
            strndup(start, (size_t)(p - start));

        p++;
    }

    if (!count)
    {
        emit_error(
            emit, ctx,
            "multiWatch requires quoted commands");
        return 2;
    }

    int status = 0;

    for (;;)
    {
        if (e->cancel && *e->cancel)
        {
            status = 130;
            break;
        }

        int fds[8][2];
        pid_t kids[8] = {0};
        pid_t group = 0;
        int launched = 0;

        for (int i = 0; i < count; i++)
        {
            if (pipe(fds[i]) < 0)
                break;

            pid_t pid = fork();

            if (pid == 0)
            {
                if (group)
                    setpgid(0, group);
                else
                    setpgid(0, 0);

                dup2(fds[i][1], STDOUT_FILENO);
                dup2(fds[i][1], STDERR_FILENO);

                close(fds[i][0]);
                close(fds[i][1]);

                if (chdir(e->cwd) < 0)
                    _exit(1);

                char *words[MAXTOK] = {0};
                int n = 0;

                if (tokenize(commands[i],
                             words, &n, e) < 0 || !n)
                    _exit(2);

                words[n] = NULL;

                execvp(words[0], words);
                perror(words[0]);
                _exit(127);
            }

            close(fds[i][1]);

            if (pid < 0)
            {
                close(fds[i][0]);
                break;
            }

            if (!group)
                group = pid;

            setpgid(pid, group);
            kids[i] = pid;
            launched++;

            int flags = fcntl(fds[i][0], F_GETFL);
            fcntl(fds[i][0], F_SETFL,
                  flags | O_NONBLOCK);
        }

        if (current)
            *current = group;

        struct pollfd pf[8];

        for (int i = 0; i < launched; i++)
        {
            pf[i] = (struct pollfd){
                fds[i][0], POLLIN | POLLHUP, 0
            };
        }

        int open_count = launched;

        while (open_count)
        {
            int r = poll(pf, launched, 100);

            if (r < 0 && errno == EINTR)
                continue;

            for (int i = 0; i < launched; i++)
            {
                if (pf[i].fd >= 0 &&
                    (pf[i].revents &
                     (POLLIN | POLLHUP | POLLERR)))
                {
                    char buf[900];
                    ssize_t n;

                    while ((n = read(
                                pf[i].fd, buf,
                                sizeof(buf) - 1)) > 0)
                    {
                        buf[n] = '\0';

                        char title[100];

                        snprintf(title, sizeof(title),
                                 "[%d] %s: ",
                                 i + 1, commands[i]);

                        emit(ctx, title);
                        emit(ctx, buf);
                    }

                    if (n == 0 ||
                        (pf[i].revents &
                         (POLLHUP | POLLERR)))
                    {
                        close(pf[i].fd);
                        pf[i].fd = -1;
                        open_count--;
                    }
                }
            }
        }

        int interrupted = 0;

        for (int i = 0; i < launched; i++)
        {
            int w = 0;
            pid_t r;

            do {
                r = waitpid(kids[i], &w, 0);
            } while (r < 0 && errno == EINTR);

            if (r > 0 && WIFSIGNALED(w) &&
                WTERMSIG(w) == SIGINT)
                interrupted = 1;
        }

        if (interrupted || launched != count)
        {
            status = interrupted ? 130 : 1;
            break;
        }

        struct pollfd idle = {-1, 0, 0};

        for (int tick = 0; tick < 20; tick++)
        {
            if (e->cancel && *e->cancel)
            {
                status = 130;
                break;
            }

            poll(&idle, 0, 100);
        }

        if (status == 130)
            break;
    }

    if (current)
        *current = 0;

    for (int i = 0; i < count; i++)
        free(commands[i]);

    return status;
}

/* ---------- Main engine entry point ---------- */

int engine_run(
    Engine *e,
    const char *input,
    EngineEmit emit,
    void *ctx,
    volatile pid_t *current)
{
    reap_jobs(e);

    if (e->cancel)
        *e->cancel = 0;

    while (isspace((unsigned char)*input))
        input++;

    if (!strncmp(input, "multiWatch", 10) &&
        (isspace((unsigned char)input[10]) ||
         input[10] == '['))
    {
        return e->last_status =
            multiwatch(e, input, emit, ctx, current);
    }

    char *tok[MAXTOK] = {0};
    int n = 0;

    if (tokenize(input, tok, &n, e) < 0)
    {
        emit_error(
            emit, ctx,
            "Parse error: unmatched quote or too many tokens");

        free_tokens(tok, n);
        return e->last_status = 2;
    }

    if (!n)
    {
        free_tokens(tok, n);
        return 0;
    }

    Pipeline *p = calloc(1, sizeof(*p));

    if (!p)
    {
        free_tokens(tok, n);
        return 1;
    }

    if (parse(tok, n, p) < 0)
    {
        emit_error(
            emit, ctx,
            "Parse error: invalid pipeline or redirection");

        free(p);
        free_tokens(tok, n);

        return e->last_status = 2;
    }

    int r = execute_pipeline(
        e, p, input, emit, ctx, current);

    free(p);
    free_tokens(tok, n);

    return e->last_status = r;
}
