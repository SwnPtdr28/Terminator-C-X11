
#define _POSIX_C_SOURCE 200809L

#include "history.h"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HISTORY_LIMIT 10000
#define HISTORY_WIDTH 1024

static int open_history(void)
{
    const char *home = getenv("HOME");
    char path[4096];

    int n = snprintf(path, sizeof(path),
                     "%s/.terminator_history",
                     home ? home : "/tmp");

    if (n < 0 || (size_t)n >= sizeof(path))
        return -1;

    return open(path, O_CREAT | O_RDWR, 0600);
}

static void free_history(char **lines, int count)
{
    if (!lines)
        return;

    for (int i = 0; i < count; i++)
        free(lines[i]);

    free(lines);
}

static char **read_lines(FILE *f, int *count)
{
    *count = 0;

    char **lines = calloc(HISTORY_LIMIT,
                          sizeof(char *));

    if (!lines)
        return NULL;

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    rewind(f);

    while ((n = getline(&line, &cap, f)) >= 0)
    {
        if (n > 0 && line[n - 1] == '\n')
            line[--n] = '\0';

        if (n >= HISTORY_WIDTH)
            line[HISTORY_WIDTH - 1] = '\0';

        if (*count == HISTORY_LIMIT)
        {
            free(lines[0]);

            memmove(lines, lines + 1,
                    (HISTORY_LIMIT - 1) *
                    sizeof(char *));

            (*count)--;
        }

        char *copy = strdup(line);

        if (!copy)
            break;

        lines[(*count)++] = copy;
    }

    free(line);
    return lines;
}

static char **load_history(int *count)
{
    *count = 0;

    int fd = open_history();

    if (fd < 0)
        return NULL;

    if (flock(fd, LOCK_SH) != 0)
    {
        close(fd);
        return NULL;
    }

    FILE *f = fdopen(fd, "r");

    if (!f)
    {
        flock(fd, LOCK_UN);
        close(fd);
        return NULL;
    }

    char **lines = read_lines(f, count);

    flock(fd, LOCK_UN);
    fclose(f);

    return lines;
}

void history_add(const char *command)
{
    if (!command || !*command ||
        strchr(command, '\n'))
        return;

    int fd = open_history();

    if (fd < 0)
        return;

    if (flock(fd, LOCK_EX) != 0)
    {
        close(fd);
        return;
    }

    FILE *f = fdopen(fd, "r+");

    if (!f)
    {
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    int count = 0;
    char **lines = read_lines(f, &count);

    if (!lines)
    {
        flock(fd, LOCK_UN);
        fclose(f);
        return;
    }

    if (count == HISTORY_LIMIT)
    {
        free(lines[0]);

        memmove(lines, lines + 1,
                (HISTORY_LIMIT - 1) *
                sizeof(char *));

        count--;
    }

    char *new_command = strndup(command,
                               HISTORY_WIDTH - 1);

    if (new_command)
        lines[count++] = new_command;

    /*
     * Rewrite the history under an exclusive lock.
     * The file now contains at most 10000 commands.
     */
    if (ftruncate(fd, 0) == 0 &&
        fseek(f, 0, SEEK_SET) == 0)
    {
        for (int i = 0; i < count; i++)
            fprintf(f, "%s\n", lines[i]);

        fflush(f);
    }

    free_history(lines, count);

    flock(fd, LOCK_UN);
    fclose(f);
}

int history_get(int back, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;

    int count = 0;
    char **lines = load_history(&count);

    if (!lines)
        return 0;

    int found = back >= 0 && back < count;

    if (found)
        snprintf(out, cap, "%s",
                 lines[count - 1 - back]);

    free_history(lines, count);

    return found;
}

void history_print(void (*emit)(void *, const char *),
                   void *ctx)
{
    if (!emit)
        return;

    int count = 0;
    char **lines = load_history(&count);

    if (!lines)
        return;

    int start = count > 1000 ? count - 1000 : 0;

    for (int i = start; i < count; i++)
    {
        char buffer[1200];

        snprintf(buffer, sizeof(buffer),
                 "%d  %s\n", i + 1, lines[i]);

        emit(ctx, buffer);
    }

    free_history(lines, count);
}

/* Longest common contiguous substring length. */
static int common_substring(const char *a,
                            const char *b)
{
    size_t alen = strlen(a);
    size_t blen = strlen(b);

    if (alen >= HISTORY_WIDTH)
        alen = HISTORY_WIDTH - 1;

    if (blen >= HISTORY_WIDTH)
        blen = HISTORY_WIDTH - 1;

    int previous[HISTORY_WIDTH] = {0};
    int current[HISTORY_WIDTH] = {0};
    int best = 0;

    for (size_t i = 1; i <= alen; i++)
    {
        for (size_t j = 1; j <= blen; j++)
        {
            if (a[i - 1] == b[j - 1])
            {
                current[j] = previous[j - 1] + 1;

                if (current[j] > best)
                    best = current[j];
            }
            else
            {
                current[j] = 0;
            }
        }

        memcpy(previous, current,
               sizeof(previous));

        memset(current, 0, sizeof(current));
    }

    return best;
}

int history_search(const char *term,
                   char *out, size_t cap)
{
    if (!term || !*term || !out || cap == 0)
        return 0;

    int count = 0;
    char **lines = load_history(&count);

    if (!lines)
        return 0;

    int best_index = -1;
    int best_score = 2;

    /* Exact match: most recent command wins. */
    for (int i = count - 1; i >= 0; i--)
    {
        if (strcmp(lines[i], term) == 0)
        {
            snprintf(out, cap, "%s", lines[i]);
            free_history(lines, count);
            return 2;
        }
    }

    /* Otherwise use longest common substring. */
    for (int i = count - 1; i >= 0; i--)
    {
        int score = common_substring(term, lines[i]);

        if (score > best_score)
        {
            best_score = score;
            best_index = i;
        }
    }

    if (best_index >= 0)
        snprintf(out, cap, "%s", lines[best_index]);

    free_history(lines, count);

    return best_index >= 0 ? 1 : 0;
}
