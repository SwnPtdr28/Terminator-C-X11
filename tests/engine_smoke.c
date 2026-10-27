#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
static void output(void *c,const char *s){(void)c;fputs(s,stdout);fflush(stdout);}
int main(int argc,char **argv){Engine *e=calloc(1,sizeof(*e));getcwd(e->cwd,sizeof(e->cwd));for(int i=1;i<argc;i++){printf("TEST: %s\n",argv[i]);int r=engine_run(e,argv[i],output,NULL,NULL);printf("STATUS=%d\n",r);}free(e);return 0;}
