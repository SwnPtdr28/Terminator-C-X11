#ifndef TABS_H
#define TABS_H
#include "terminator.h"
extern int process_mode;
extern Tab *tabs[MAX_TABS];
extern int tab_count;
extern int active;
extern int wake_pipe[2];
int create_tab(void);
void destroy_tab(Tab *t);
void close_active_tab(void);
#endif
