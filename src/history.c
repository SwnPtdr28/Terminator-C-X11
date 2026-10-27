#define _POSIX_C_SOURCE 200809L
#include "history.h"
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HISTORY_LIMIT 10000
#define HISTORY_WIDTH 1024
static int open_history(void) {
    const char *home=getenv("HOME"); char path[4096];
    snprintf(path,sizeof(path),"%s/.terminator_history",home?home:"/tmp");
    return open(path,O_CREAT|O_RDWR,0600);
}
void history_add(const char *command) {
    if(!command || !*command || strchr(command,'\n')) return;
    int fd=open_history(); if(fd<0)return;
    if(flock(fd,LOCK_EX)==0) {
        lseek(fd,0,SEEK_END); write(fd,command,strlen(command));write(fd,"\n",1);
        flock(fd,LOCK_UN);
    }
    close(fd);
}
static char **load_history(int *count) {
    *count=0; char **lines=calloc(HISTORY_LIMIT,sizeof(char*));
    if(!lines)return NULL;
    int fd=open_history(); if(fd<0)return lines;
    if(flock(fd,LOCK_SH)!=0){close(fd);return lines;}
    FILE *f=fdopen(fd,"r"); if(!f){flock(fd,LOCK_UN);close(fd);return lines;}
    char *line=NULL;size_t cap=0;ssize_t n;
    while((n=getline(&line,&cap,f))>=0) {
        if(n && line[n-1]=='\n')line[--n]='\0';
        if(n>=HISTORY_WIDTH)line[HISTORY_WIDTH-1]='\0';
        if(*count==HISTORY_LIMIT){free(lines[0]);memmove(lines,lines+1,(HISTORY_LIMIT-1)*sizeof(char*));--*count;}
        lines[(*count)++]=strdup(line);
    }
    free(line);flock(fd,LOCK_UN);fclose(f);return lines;
}
int history_get(int back,char *out,size_t cap) {
    int count;char **lines=load_history(&count);if(!lines)return 0;
    int found=back>=0 && back<count;
    if(found)snprintf(out,cap,"%s",lines[count-1-back]);
    for(int i=0;i<count;i++) free(lines[i]);
    free(lines);
    return found;
}
void history_print(void (*emit)(void *,const char *),void *ctx) {
    int count;char **lines=load_history(&count);if(!lines)return;
    int start=count>1000?count-1000:0;
    for(int i=start;i<count;i++){
        char buffer[1200];snprintf(buffer,sizeof(buffer),"%d  %s\n",i+1,lines[i]);emit(ctx,buffer);
    }
    for(int i=0;i<count;i++) free(lines[i]);
    free(lines);
}
