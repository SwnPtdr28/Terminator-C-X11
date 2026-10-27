#ifndef GUI_H
#define GUI_H

#include <X11/Xlib.h>
#include "terminator.h"

void draw_gui(Display *display, Window window, GC gc,
              int screen, int width, int height,
              Tab *tabs[], int tab_count, int active);

#endif
