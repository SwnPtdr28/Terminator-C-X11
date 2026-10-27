#include "gui.h"
#include <stdio.h>
#include <string.h>

void draw_gui(Display *display, Window window, GC gc, int screen, int width, int height,
              Tab *tabs[], int tab_count, int active) {
    XClearWindow(display,window);
    XSetForeground(display,gc,WhitePixel(display,screen));
    for (int i=0;i<tab_count;i++) {
        char name[32];
        snprintf(name,sizeof(name),"[%d] Tab %d",i+1,i+1);
        if (i==active) XFillRectangle(display,window,gc,10+i*112,8,104,27);
        XSetForeground(display,gc,i==active?BlackPixel(display,screen):WhitePixel(display,screen));
        XDrawString(display,window,gc,18+i*112,27,name,(int)strlen(name));
        XSetForeground(display,gc,WhitePixel(display,screen));
    }
    if (!tab_count) return;
    Tab *t=tabs[active];
    pthread_mutex_lock(&t->lock);
    int visible=(height-100)/LINE_HEIGHT;
    if (visible<1) visible=1;
    int end=t->line_count-t->scroll;
    if (end<0) end=0;
    int start=end-visible;
    if (start<0) start=0;
    int y=58;
    for (int i=start;i<end;i++) {
        XDrawString(display,window,gc,12,y,t->lines[i],(int)strlen(t->lines[i]));
        y+=LINE_HEIGHT;
    }
    char prompt[INPUT_LEN+80];
    snprintf(prompt,sizeof(prompt),"%s$ %s",t->running?"[running] ":"",t->input);
    XDrawLine(display,window,gc,0,height-43,width,height-43);
    XDrawString(display,window,gc,12,height-22,prompt,(int)strlen(prompt));
    pthread_mutex_unlock(&t->lock);
    XFlush(display);
}

