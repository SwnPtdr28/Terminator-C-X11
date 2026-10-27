#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

int shell_cd(char cwd[PATH_MAX], int argc, char *argv[],
             char *error, size_t error_size)
{
    const char *target;
    char expanded[PATH_MAX];
    char resolved[PATH_MAX];
    char candidate[PATH_MAX];

    if (argc > 2) {
        snprintf(error, error_size, "cd: too many arguments\n");
        return 1;
    }

    if (argc == 1 || strcmp(argv[1], "~") == 0) {
        target = getenv("HOME");
        if (!target) {
            snprintf(error, error_size, "cd: HOME not set\n");
            return 1;
        }
    } else if (strncmp(argv[1], "~/", 2) == 0) {
        const char *home = getenv("HOME");
        if (!home) {
            snprintf(error, error_size, "cd: HOME not set\n");
            return 1;
        }
        if (snprintf(expanded, sizeof(expanded), "%s/%s", home, argv[1] + 2)
                >= (int)sizeof(expanded)) {
            snprintf(error, error_size, "cd: path too long\n");
            return 1;
        }
        target = expanded;
    } else {
        target = argv[1];
    }

    if (target[0] == '/') {
        if (snprintf(candidate, sizeof(candidate), "%s", target)
                >= (int)sizeof(candidate)) {
            snprintf(error, error_size, "cd: path too long\n");
            return 1;
        }
    } else {
        if (snprintf(candidate, sizeof(candidate), "%s/%s", cwd, target)
                >= (int)sizeof(candidate)) {
            snprintf(error, error_size, "cd: path too long\n");
            return 1;
        }
    }

    if (!realpath(candidate, resolved)) {
        snprintf(error, error_size, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(error, error_size, "cd: %s: not a directory\n", target);
        return 1;
    }
    snprintf(cwd, PATH_MAX, "%s", resolved);
    return 0;
}

void shell_expand_status(int argc, char *argv[], int previous_status,
                         char status_text[32])
{
    snprintf(status_text, 32, "%d", previous_status);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "$?") == 0)
            argv[i] = status_text;
    }
}
