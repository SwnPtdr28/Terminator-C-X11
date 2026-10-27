#ifndef ENGINE_H
#define ENGINE_H
#include <limits.h>
#include <sys/types.h>
#include <signal.h>
#define ENGINE_JOBS 32
typedef struct {pid_t pid; int state; char command[256];} EngineJob;
typedef struct {char cwd[PATH_MAX]; int last_status; volatile sig_atomic_t *cancel; EngineJob jobs[ENGINE_JOBS];} Engine;
typedef void (*EngineEmit)(void *, const char *);
int engine_run(Engine *, const char *, EngineEmit, void *, volatile pid_t *);
#endif
