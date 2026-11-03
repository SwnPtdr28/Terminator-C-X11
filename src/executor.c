#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "tabs.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

void notify_gui(void){char c='x';if(wake_pipe[1]>=0)(void)write(wake_pipe[1],&c,1);}
void add_line_locked(Tab *t,const char *s){
    if(t->line_count==MAX_LINES){memmove(t->lines[0],t->lines[1],(MAX_LINES-1)*sizeof(t->lines[0]));t->line_count--;}
    snprintf(t->lines[t->line_count++],LINE_LEN,"%s",s);t->scroll=0;
}
void append_text_locked(Tab *t,const char *s){
    if (!t->line_count)
    add_line_locked(t, "");
    for(const unsigned char *p=(const unsigned char*)s;*p;p++){
        if(*p=='\n')add_line_locked(t,"");
        else if(*p=='\r')t->lines[t->line_count-1][0]=0;
        else if(*p=='\t'){for(int i=0;i<4;i++){size_t n=strlen(t->lines[t->line_count-1]);if(n<LINE_LEN-1){t->lines[t->line_count-1][n]=' ';t->lines[t->line_count-1][n+1]=0;}}}
        else if(*p>=32){size_t n=strlen(t->lines[t->line_count-1]);if(n>=LINE_LEN-1)add_line_locked(t,"");n=strlen(t->lines[t->line_count-1]);t->lines[t->line_count-1][n]=(char)*p;t->lines[t->line_count-1][n+1]=0;}
    }
}
int write_full(int fd,const void *b,size_t len){const char *p=b;while(len){ssize_t n=write(fd,p,len);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;len-=(size_t)n;}return 0;}
static void thread_emit(void *ctx,const char *s){Tab *t=ctx;pthread_mutex_lock(&t->lock);append_text_locked(t,s);pthread_mutex_unlock(&t->lock);notify_gui();}
void *worker(void *arg){
    Tab *t=arg;Engine *e=calloc(1,sizeof(*e));if(!e)return NULL;snprintf(e->cwd,sizeof(e->cwd),"%s",t->cwd);e->cancel=&t->cancel;
    pthread_mutex_lock(&t->lock);
    while(!t->stop){
        while(!t->pending&&!t->stop)pthread_cond_wait(&t->ready,&t->lock);
        if(t->stop)break;
        char cmd[INPUT_LEN];snprintf(cmd,sizeof(cmd),"%s",t->command);t->pending=0;t->running=1;
        pthread_mutex_unlock(&t->lock);
        int r=engine_run(e,cmd,thread_emit,t,&t->child_pid);
        pthread_mutex_lock(&t->lock);t->exit_status=r;t->running=0;t->child_pid=0;snprintf(t->cwd,sizeof(t->cwd),"%s",e->cwd);
        notify_gui();
    }
    pthread_mutex_unlock(&t->lock);free(e);return NULL;
}
typedef struct {char type;int status;char text[900];} Packet;
typedef struct {int fd;} ProcessCtx;
static void process_emit(void *ctx,const char *s){
    ProcessCtx *pc=ctx;while(*s){Packet p={.type='O'};size_t n=strlen(s);if(n>=sizeof(p.text))n=sizeof(p.text)-1;memcpy(p.text,s,n);if(write_full(pc->fd,&p,sizeof(p))<0)break;s+=n;}
}
static volatile pid_t process_foreground=0;
static volatile sig_atomic_t process_cancel=0;
static void forward_signal(int sig){
    process_cancel=1;
    pid_t p=process_foreground;
    if(p>0)kill(-p,sig==SIGUSR1?SIGINT:SIGTSTP);
}
void shell_process(int cmd_fd,int msg_fd){
    struct sigaction sa={0};sa.sa_handler=forward_signal;sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1,&sa,NULL);sigaction(SIGUSR2,&sa,NULL);

    Engine *e=calloc(1,sizeof(*e));if(!e)_exit(1);
    if(!getcwd(e->cwd,sizeof(e->cwd)))_exit(1);e->cancel=&process_cancel;
    ProcessCtx pc={msg_fd};char cmd[INPUT_LEN];size_t used=0;
    for(;;){char c;ssize_t n=read(cmd_fd,&c,1);if(n<0&&errno==EINTR)continue;if(n<=0)break;
        if(c!='\n'){if(used<sizeof(cmd)-1)cmd[used++]=c;continue;}
        cmd[used]=0;used=0;int r=engine_run(e,cmd,process_emit,&pc,&process_foreground);
        Packet done={.type='D',.status=r};if(write_full(msg_fd,&done,sizeof(done))<0)break;
    }
    free(e);close(cmd_fd);close(msg_fd);_exit(0);
}
void consume_messages(Tab *t){
    for(;;){ssize_t n=read(t->msg_read,t->msg_buf+t->msg_used,sizeof(t->msg_buf)-t->msg_used);
        if(n>0)t->msg_used+=(size_t)n;
        else if(n==0){close(t->msg_read);t->msg_read=-1;t->running=0;break;}
        else if(errno==EINTR)continue;else break;
        while(t->msg_used>=sizeof(Packet)){
            Packet p;memcpy(&p,t->msg_buf,sizeof(p));memmove(t->msg_buf,t->msg_buf+sizeof(p),t->msg_used-sizeof(p));t->msg_used-=sizeof(p);
            pthread_mutex_lock(&t->lock);if(p.type=='O')append_text_locked(t,p.text);if(p.type=='D'){t->running=0;t->exit_status=p.status;}pthread_mutex_unlock(&t->lock);
        }
    }
}
