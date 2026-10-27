#ifndef HISTORY_H
#define HISTORY_H
#include <stddef.h>
void history_add(const char *command);
int history_get(int back, char *out, size_t cap);
void history_print(void (*emit)(void *,const char *), void *ctx);
#endif
