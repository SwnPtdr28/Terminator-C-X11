#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include <limits.h>

/* Update a tab's directory, not the GUI process's working directory.
   Returns 0 on success and 1 on failure. */
int shell_cd(char cwd[PATH_MAX], int argc, char *argv[],
             char *error, size_t error_size);

/* Basic Task 2 support for $? as a standalone argument. */
void shell_expand_status(int argc, char *argv[], int previous_status,
                         char status_text[32]);

#endif
