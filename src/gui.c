#define _POSIX_C_SOURCE 200809L
#include "gui.h"
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

extern int history_search_mode;
extern int selection_start, selection_end, selection_tab;

static unsigned long gui_colors[6];
static Display *color_display = NULL;

static unsigned long load_color(Display *d, int screen, const char *hex)
{
    XColor c;
    Colormap map = DefaultColormap(d, screen);
    if (XParseColor(d, map, hex, &c) && XAllocColor(d, map, &c))
        return c.pixel;
    return WhitePixel(d, screen);
}

static void init_colors(Display *d, int screen)
{
    if (color_display == d) return;
    gui_colors[0] = load_color(d, screen, "#101820");
    gui_colors[1] = load_color(d, screen, "#1C2633");
    gui_colors[2] = load_color(d, screen, "#304052");
    gui_colors[3] = load_color(d, screen, "#E5E7EB");
    gui_colors[4] = load_color(d, screen, "#9CA3AF");
    gui_colors[5] = load_color(d, screen, "#4ADE80");
    color_display = d;
}

void draw_gui(Display *display, Window window, GC gc,
              int screen, int width, int height,
              Tab *tabs[], int tab_count, int active)
{
    if (width <= 0 || height <= 0) return;
    init_colors(display, screen);

    unsigned long background = gui_colors[0];
    unsigned long tab_bg = gui_colors[1];
    unsigned long active_bg = gui_colors[2];
    unsigned long foreground = gui_colors[3];
    unsigned long secondary = gui_colors[4];
    unsigned long accent = gui_colors[5];

    Pixmap buffer = XCreatePixmap(display, window,
        (unsigned int)width, (unsigned int)height,
        (unsigned int)DefaultDepth(display, screen));
    if (!buffer) return;

    XSetForeground(display, gc, background);
    XFillRectangle(display, buffer, gc, 0, 0,
                   (unsigned int)width, (unsigned int)height);
    XSetForeground(display, gc, tab_bg);
    XFillRectangle(display, buffer, gc, 0, 0,
                   (unsigned int)width, 52);

    for (int i = 0; i < tab_count; i++)
    {
        int x = 12 + i * 130;
        char label[32];
        snprintf(label, sizeof(label), "Tab %d", i + 1);
        if (i == active)
        {
            XSetForeground(display, gc, active_bg);
            XFillRectangle(display, buffer, gc, x, 8, 120, 36);
            XSetForeground(display, gc, accent);
            XFillRectangle(display, buffer, gc, x, 44, 120, 3);
        }
        XSetForeground(display, gc,
                       i == active ? foreground : secondary);
        XDrawString(display, buffer, gc, x + 14, 32,
                    label, (int)strlen(label));
    }

    if (tab_count > 0 && active >= 0 && active < tab_count)
    {
        Tab *t = tabs[active];
        pthread_mutex_lock(&t->lock);

        int output_top = 82;
        int input_top = height - 68;
        int visible = (input_top - output_top) / LINE_HEIGHT;
        if (visible < 1) visible = 1;
        int end = t->line_count - t->scroll;
        if (end < 0) end = 0;
        int start = end - visible;
        if (start < 0) start = 0;

        int first = selection_start < selection_end ? selection_start : selection_end;
        int last = selection_start > selection_end ? selection_start : selection_end;
        int y = output_top;
        for (int i = start; i < end; i++)
        {
            int selected = selection_tab == active &&
                selection_start >= 0 && selection_end >= 0 &&
                i >= first && i <= last;
            if (selected)
            {
                XSetForeground(display, gc, active_bg);
                XFillRectangle(display, buffer, gc, 14,
                               y - LINE_HEIGHT + 5,
                               (unsigned int)(width > 28 ? width - 28 : 0),
                               (unsigned int)LINE_HEIGHT);
            }
            XSetForeground(display, gc, selected ? accent : foreground);
            XDrawString(display, buffer, gc, 18, y,
                        t->lines[i], (int)strlen(t->lines[i]));
            y += LINE_HEIGHT;
        }

        XSetForeground(display, gc, tab_bg);
        XFillRectangle(display, buffer, gc, 0, input_top,
                       (unsigned int)width,
                       (unsigned int)(height - input_top));

        const char *prompt = history_search_mode
            ? "Search history: "
            : (t->running ? "[running] $ " : "$ ");
        int baseline = height - 27;
        XSetForeground(display, gc, accent);
        XDrawString(display, buffer, gc, 18, baseline,
                    prompt, (int)strlen(prompt));

        XFontStruct *font = XQueryFont(display, XGContextFromGC(gc));
        int prompt_width = (int)strlen(prompt) * 10;
        int before_cursor = t->cursor * 10;
        int cursor_height = 22;
        if (font)
        {
            prompt_width = XTextWidth(font, prompt, (int)strlen(prompt));
            before_cursor = XTextWidth(font, t->input, t->cursor);
            cursor_height = font->ascent + font->descent;
        }
        int input_x = 18 + prompt_width + 8;
        XSetForeground(display, gc, foreground);
        XDrawString(display, buffer, gc, input_x, baseline,
                    t->input, t->input_len);
        XSetForeground(display, gc, accent);
        XFillRectangle(display, buffer, gc,
                       input_x + before_cursor,
                       baseline - (font ? font->ascent : 17),
                       2, (unsigned int)cursor_height);
        if (font) XFreeFontInfo(NULL, font, 1);
        pthread_mutex_unlock(&t->lock);
    }

    XCopyArea(display, buffer, window, gc, 0, 0,
              (unsigned int)width, (unsigned int)height, 0, 0);
    XFreePixmap(display, buffer);
    XFlush(display);
}
