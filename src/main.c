
#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "terminator.h"
#include "gui.h"
#include "tabs.h"
#include "executor.h"
#include "history.h"

static Display *display;
static Window window;
static GC gc;
static Atom wm_delete;
static int screen;
static int width = 850;
static int height = 550;

/* Read by gui.c to display the search prompt. */
int history_search_mode = 0;

/* Save the previous input while searching. */
static Tab *search_tab = NULL;
static char saved_input[INPUT_LEN];
static int saved_cursor = 0;

static void set_input(Tab *t, const char *s)
{
    snprintf(t->input, sizeof(t->input), "%s", s);
    t->input_len = (int)strlen(t->input);
    t->cursor = t->input_len;
}

static void cancel_search(void)
{
    if (!history_search_mode)
        return;

    if (search_tab)
    {
        pthread_mutex_lock(&search_tab->lock);

        set_input(search_tab, saved_input);

        if (saved_cursor >= 0 &&
            saved_cursor <= search_tab->input_len)
            search_tab->cursor = saved_cursor;

        pthread_mutex_unlock(&search_tab->lock);
    }

    history_search_mode = 0;
    search_tab = NULL;
}

static void begin_search(Tab *t)
{
    if (history_search_mode)
        return;

    pthread_mutex_lock(&t->lock);

    snprintf(saved_input, sizeof(saved_input),
             "%s", t->input);

    saved_cursor = t->cursor;

    set_input(t, "");

    search_tab = t;
    history_search_mode = 1;

    pthread_mutex_unlock(&t->lock);

    notify_gui();
}

static void finish_search(Tab *t)
{
    char term[INPUT_LEN];
    char result[INPUT_LEN];

    pthread_mutex_lock(&t->lock);

    snprintf(term, sizeof(term), "%s", t->input);

    pthread_mutex_unlock(&t->lock);

    int found = history_search(term, result,
                               sizeof(result));

    pthread_mutex_lock(&t->lock);

    if (found)
    {
        set_input(t, result);
        t->history_pos = 0;
    }
    else
    {
        set_input(t, saved_input);

        if (saved_cursor >= 0 &&
            saved_cursor <= t->input_len)
            t->cursor = saved_cursor;

        add_line_locked(t,
            "No match for search term in history");

        /* Preserve the blank output line. */
        add_line_locked(t, "");
    }

    pthread_mutex_unlock(&t->lock);

    history_search_mode = 0;
    search_tab = NULL;

    notify_gui();
}

static int has_unclosed_quote(const char *s)
{
    char quote = 0;

    for (size_t i = 0; s[i]; i++)
    {
        if (s[i] == '\\' &&
            quote != '\'' && s[i + 1])
        {
            i++;
            continue;
        }

        if (s[i] == '\'' || s[i] == '"')
        {
            if (!quote)
                quote = s[i];
            else if (quote == s[i])
                quote = 0;
        }
    }

    return quote != 0;
}

static void submit(Tab *t)
{
    pthread_mutex_lock(&t->lock);

    if (t->running || t->pending)
    {
        add_line_locked(t, "Command still running");
        pthread_mutex_unlock(&t->lock);
        notify_gui();
        return;
    }

    char command[INPUT_LEN];

    snprintf(command, sizeof(command),
             "%s", t->input);

    size_t len = strlen(command);
    int backslash_continuation = 0;

    if (len > 0)
    {
        size_t slashes = 0;

        while (slashes < len &&
               command[len - 1 - slashes] == '\\')
            slashes++;

        if (slashes % 2 == 1)
        {
            backslash_continuation = 1;
            command[--len] = '\0';
        }
    }

    char combined[INPUT_LEN];

    snprintf(combined, sizeof(combined),
             "%s", t->continuation);

    size_t used = strlen(combined);
    size_t available = sizeof(combined) - used - 1;

    if (available > 0)
        strncat(combined, command, available);

    if (backslash_continuation ||
        has_unclosed_quote(combined))
    {
        size_t combined_len = strlen(combined);

        if (combined_len >=
            sizeof(t->continuation) - 2)
        {
            add_line_locked(t, "Continuation too long");
            t->continuation[0] = '\0';
        }
        else
        {
            memcpy(t->continuation,
                   combined, combined_len);

            t->continuation[combined_len] = ' ';
            t->continuation[combined_len + 1] = '\0';

            add_line_locked(t, "> ");
        }

        t->input[0] = '\0';
        t->input_len = 0;
        t->cursor = 0;

        pthread_mutex_unlock(&t->lock);
        notify_gui();
        return;
    }

    t->continuation[0] = '\0';

    char *start = combined;

    while (*start == ' ' || *start == '\t')
        start++;

    char *end = start + strlen(start);

    while (end > start &&
           (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    *end = '\0';

    if (strcmp(start, "exit") == 0)
    {
        pthread_mutex_unlock(&t->lock);
        close_active_tab();
        notify_gui();
        return;
    }

    char display_command[INPUT_LEN + 4];

    snprintf(display_command,
             sizeof(display_command),
             "$ %s", combined);

    add_line_locked(t, display_command);
    add_line_locked(t, "");

    snprintf(t->command, sizeof(t->command),
             "%s", combined);

    history_add(combined);
    t->history_pos = 0;

    if (process_mode)
    {
        t->running = 1;

        char message[INPUT_LEN + 2];

        snprintf(message, sizeof(message),
                 "%s\n", combined);

        if (write_full(t->cmd_write,
                       message, strlen(message)) < 0)
        {
            t->running = 0;

            add_line_locked(t,
                "Tab process is unavailable");
        }
    }
    else
    {
        t->pending = 1;
        pthread_cond_signal(&t->ready);
    }

    t->input[0] = '\0';
    t->input_len = 0;
    t->cursor = 0;

    pthread_mutex_unlock(&t->lock);

    notify_gui();
}

static void handle_search_key(
    Tab *t, KeySym sym, const char *chars,
    int n, int ctrl)
{
    if (sym == XK_Escape ||
        (ctrl && (sym == XK_g || sym == XK_G)))
    {
        cancel_search();
        notify_gui();
        return;
    }

    if (sym == XK_Return || sym == XK_KP_Enter)
    {
        finish_search(t);
        return;
    }

    pthread_mutex_lock(&t->lock);

    if (sym == XK_BackSpace)
    {
        if (t->cursor > 0)
        {
            memmove(t->input + t->cursor - 1,
                    t->input + t->cursor,
                    (size_t)(t->input_len -
                             t->cursor + 1));

            t->cursor--;
            t->input_len--;
        }
    }
    else if (sym == XK_Delete)
    {
        if (t->cursor < t->input_len)
        {
            memmove(t->input + t->cursor,
                    t->input + t->cursor + 1,
                    (size_t)(t->input_len -
                             t->cursor));

            t->input_len--;
        }
    }
    else if (sym == XK_Left)
    {
        if (t->cursor > 0)
            t->cursor--;
    }
    else if (sym == XK_Right)
    {
        if (t->cursor < t->input_len)
            t->cursor++;
    }
    else if (sym == XK_Home)
    {
        t->cursor = 0;
    }
    else if (sym == XK_End)
    {
        t->cursor = t->input_len;
    }
    else if (!ctrl && n > 0)
    {
        for (int i = 0; i < n; i++)
        {
            if ((unsigned char)chars[i] >= 32 &&
                t->input_len < INPUT_LEN - 1)
            {
                memmove(t->input + t->cursor + 1,
                        t->input + t->cursor,
                        (size_t)(t->input_len -
                                 t->cursor + 1));

                t->input[t->cursor++] = chars[i];
                t->input_len++;
            }
        }
    }

    pthread_mutex_unlock(&t->lock);
    notify_gui();
}

static void handle_key(XKeyEvent *e)
{
    if (tab_count <= 0)
        return;

    KeySym sym;
    char chars[64];

    int n = XLookupString(e, chars,
                          sizeof(chars), &sym, NULL);

    int ctrl = (e->state & ControlMask) != 0;
    int shift = (e->state & ShiftMask) != 0;

    Tab *t = tabs[active];

    /*
     * While searching, keyboard input belongs to
     * the search prompt, not the command executor.
     */
    if (history_search_mode)
    {
        handle_search_key(t, sym, chars, n, ctrl);
        return;
    }

    if (ctrl && (sym == XK_r || sym == XK_R))
    {
        begin_search(t);
        return;
    }

    if (ctrl && (sym == XK_t || sym == XK_T))
    {
        create_tab();
        return;
    }

    if (ctrl && sym == XK_Tab)
    {
        active = (active + 1) % tab_count;
        return;
    }

    if (ctrl && shift &&
        (sym == XK_W || sym == XK_w))
    {
        close_active_tab();
        return;
    }

    pthread_mutex_lock(&t->lock);

    if (sym == XK_Return || sym == XK_KP_Enter)
    {
        pthread_mutex_unlock(&t->lock);
        submit(t);
        return;
    }

    if (ctrl && (sym == XK_c || sym == XK_C))
    {
        t->cancel = 1;

        if (process_mode)
        {
            if (t->shell_pid > 0)
                kill(t->shell_pid, SIGUSR1);
        }
        else if (t->child_pid > 0)
        {
            kill(-t->child_pid, SIGINT);
        }
    }
    else if (ctrl && (sym == XK_z || sym == XK_Z))
    {
        if (process_mode)
        {
            if (t->shell_pid > 0)
                kill(t->shell_pid, SIGUSR2);
        }
        else if (t->child_pid > 0)
        {
            kill(-t->child_pid, SIGTSTP);
        }
    }
    else if (ctrl && (sym == XK_a || sym == XK_A))
    {
        t->cursor = 0;
    }
    else if (ctrl && (sym == XK_e || sym == XK_E))
    {
        t->cursor = t->input_len;
    }
    else if (ctrl && (sym == XK_u || sym == XK_U))
    {
        memmove(t->input,
                t->input + t->cursor,
                (size_t)(t->input_len -
                         t->cursor + 1));

        t->input_len -= t->cursor;
        t->cursor = 0;
    }
    else if (ctrl && (sym == XK_k || sym == XK_K))
    {
        t->input[t->cursor] = '\0';
        t->input_len = t->cursor;
    }
    else if (ctrl && (sym == XK_w || sym == XK_W))
    {
        int start = t->cursor;

        while (start > 0 &&
               t->input[start - 1] == ' ')
            start--;

        while (start > 0 &&
               t->input[start - 1] != ' ')
            start--;

        memmove(t->input + start,
                t->input + t->cursor,
                (size_t)(t->input_len -
                         t->cursor + 1));

        t->input_len -= t->cursor - start;
        t->cursor = start;
    }
    else if (ctrl && (sym == XK_l || sym == XK_L))
    {
        t->line_count = 1;
        t->lines[0][0] = '\0';
        t->scroll = 0;
    }
    else if (sym == XK_Left)
    {
        if (t->cursor > 0)
            t->cursor--;
    }
    else if (sym == XK_Right)
    {
        if (t->cursor < t->input_len)
            t->cursor++;
    }
    else if (sym == XK_Home)
    {
        t->cursor = 0;
    }
    else if (sym == XK_End)
    {
        t->cursor = t->input_len;
    }
    else if (sym == XK_Up || sym == XK_Down)
    {
        int next = t->history_pos +
                   (sym == XK_Up ? 1 : -1);

        if (next >= 1 &&
            history_get(next - 1, t->input,
                        sizeof(t->input)))
        {
            t->history_pos = next;
            t->input_len = (int)strlen(t->input);
            t->cursor = t->input_len;
        }
        else if (next == 0)
        {
            t->history_pos = 0;
            t->input[0] = '\0';
            t->input_len = 0;
            t->cursor = 0;
        }
    }
    else if (sym == XK_Tab)
    {
        int start = t->cursor;

        while (start > 0 &&
               t->input[start - 1] != ' ' &&
               t->input[start - 1] != '\t')
            start--;

        char prefix[INPUT_LEN];
        int plen = t->cursor - start;

        memcpy(prefix, t->input + start,
               (size_t)plen);

        prefix[plen] = '\0';

        DIR *dir = opendir(t->cwd);
        char match[INPUT_LEN] = "";
        int count = 0;

        if (dir)
        {
            struct dirent *de;

            while ((de = readdir(dir)))
            {
                if (strncmp(de->d_name, prefix,
                            (size_t)plen) == 0)
                {
                    if (!count)
                    {
                        snprintf(match,
                                 sizeof(match),
                                 "%s", de->d_name);
                    }
                    else
                    {
                        int k = 0;

                        while (match[k] &&
                               de->d_name[k] &&
                               match[k] == de->d_name[k])
                            k++;

                        match[k] = '\0';
                    }

                    count++;
                }
            }

            closedir(dir);
        }

        if (count && (int)strlen(match) > plen)
        {
            int extra = (int)strlen(match) - plen;

            if (t->input_len + extra < INPUT_LEN)
            {
                memmove(t->input + t->cursor + extra,
                        t->input + t->cursor,
                        (size_t)(t->input_len -
                                 t->cursor + 1));

                memcpy(t->input + start,
                       match, strlen(match));

                t->input_len += extra;
                t->cursor += extra;
            }
        }
    }
    else if (sym == XK_BackSpace)
    {
        if (t->cursor > 0)
        {
            memmove(t->input + t->cursor - 1,
                    t->input + t->cursor,
                    (size_t)(t->input_len -
                             t->cursor + 1));

            t->cursor--;
            t->input_len--;
        }
    }
    else if (sym == XK_Delete)
    {
        if (t->cursor < t->input_len)
        {
            memmove(t->input + t->cursor,
                    t->input + t->cursor + 1,
                    (size_t)(t->input_len -
                             t->cursor));

            t->input_len--;
        }
    }
    else if (!ctrl && n > 0)
    {
        for (int i = 0; i < n; i++)
        {
            if ((unsigned char)chars[i] >= 32 &&
                t->input_len < INPUT_LEN - 1)
            {
                memmove(t->input + t->cursor + 1,
                        t->input + t->cursor,
                        (size_t)(t->input_len -
                                 t->cursor + 1));

                t->input[t->cursor++] = chars[i];
                t->input_len++;
            }
        }
    }

    pthread_mutex_unlock(&t->lock);
}

int main(int argc, char **argv)
{
    if (argc == 2 &&
        strcmp(argv[1], "useProcess=True") == 0)
    {
        process_mode = 1;
    }
    else if (argc != 1)
    {
        fprintf(stderr,
                "Usage: %s [useProcess=True]\n",
                argv[0]);
        return 1;
    }

    if (process_mode)
        signal(SIGPIPE, SIG_IGN);

    if (pipe(wake_pipe) < 0)
    {
        perror("pipe");
        return 1;
    }

    int flags = fcntl(wake_pipe[1], F_GETFL, 0);
    fcntl(wake_pipe[1], F_SETFL,
          flags | O_NONBLOCK);

    display = XOpenDisplay(NULL);

    if (!display)
    {
        fprintf(stderr, "Cannot open X11 display\n");
        close(wake_pipe[0]);
        close(wake_pipe[1]);
        return 1;
    }

    screen = DefaultScreen(display);

    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        50, 50,
        (unsigned int)width,
        (unsigned int)height,
        1,
        WhitePixel(display, screen),
        BlackPixel(display, screen)
    );

    XSelectInput(
        display, window,
        ExposureMask | KeyPressMask |
        StructureNotifyMask | ButtonPressMask
    );

    wm_delete =
        XInternAtom(display, "WM_DELETE_WINDOW", False);

    XSetWMProtocols(display, window, &wm_delete, 1);

    XStoreName(
        display, window,
        process_mode
            ? "Terminator - Process Mode"
            : "Terminator - Thread Mode"
    );

    gc = XCreateGC(display, window, 0, NULL);

    const char *font_names[] = {
        "12x24",
        "10x20",
        "9x18",
        "-misc-fixed-medium-r-normal--20-200-75-75-c-100-iso8859-1",
        "fixed"
    };

    XFontStruct *terminal_font = NULL;

    for (size_t i = 0;
         i < sizeof(font_names) / sizeof(font_names[0]);
         i++)
    {
        terminal_font =
            XLoadQueryFont(display, font_names[i]);

        if (terminal_font)
            break;
    }

    if (terminal_font)
    {
        XSetFont(display, gc, terminal_font->fid);

        int font_height =
            terminal_font->ascent +
            terminal_font->descent;

        printf("Loaded terminal font height: %d pixels\n",
               font_height);
    }
    else
    {
        fprintf(stderr,
                "Warning: no requested X11 font available\n");
    }

    XMapWindow(display, window);

    if (!create_tab())
    {
        fprintf(stderr, "Cannot create tab\n");

        if (terminal_font)
            XFreeFont(display, terminal_font);

        XFreeGC(display, gc);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        close(wake_pipe[0]);
        close(wake_pipe[1]);

        return 1;
    }

    int done = 0;

    while (!done)
    {
        while (XPending(display))
        {
            XEvent e;
            XNextEvent(display, &e);

            if (e.type == ClientMessage &&
                (Atom)e.xclient.data.l[0] == wm_delete)
            {
                done = 1;
            }
            else if (e.type == ConfigureNotify)
            {
                width = e.xconfigure.width;
                height = e.xconfigure.height;
            }
            else if (e.type == KeyPress)
            {
                handle_key(&e.xkey);
            }
            else if (e.type == ButtonPress &&
                     (e.xbutton.button == 4 ||
                      e.xbutton.button == 5))
            {
                if (tab_count <= 0)
                    continue;

                Tab *t = tabs[active];

                pthread_mutex_lock(&t->lock);

                int maxscroll = t->line_count - 1;

                if (e.xbutton.button == 4)
                {
                    if (t->scroll < maxscroll)
                        t->scroll += 3;
                }
                else
                {
                    t->scroll -= 3;

                    if (t->scroll < 0)
                        t->scroll = 0;
                }

                pthread_mutex_unlock(&t->lock);
            }
        }

        if (done)
            break;

        draw_gui(display, window, gc, screen,
                 width, height,
                 tabs, tab_count, active);

        struct pollfd fds[2 + MAX_TABS] = {
            {ConnectionNumber(display), POLLIN, 0},
            {wake_pipe[0], POLLIN, 0}
        };

        int nfds = 2;

        if (process_mode)
        {
            for (int i = 0; i < tab_count; i++)
            {
                if (tabs[i]->msg_read >= 0)
                {
                    fds[nfds++] = (struct pollfd) {
                        tabs[i]->msg_read,
                        POLLIN | POLLHUP,
                        0
                    };
                }
            }
        }

        int r = poll(fds, nfds, 200);

        if (r > 0 && process_mode)
        {
            for (int i = 0; i < tab_count; i++)
            {
                if (tabs[i]->msg_read >= 0)
                    consume_messages(tabs[i]);
            }
        }

        if (r > 0 && (fds[1].revents & POLLIN))
        {
            char discard[256];

            int old =
                fcntl(wake_pipe[0], F_GETFL, 0);

            fcntl(wake_pipe[0], F_SETFL,
                  old | O_NONBLOCK);

            while (read(wake_pipe[0],
                        discard, sizeof(discard)) > 0)
            {
            }

            fcntl(wake_pipe[0], F_SETFL, old);
        }
    }

    cancel_search();

    while (tab_count)
        destroy_tab(tabs[--tab_count]);

    if (terminal_font)
        XFreeFont(display, terminal_font);

    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    close(wake_pipe[0]);
    close(wake_pipe[1]);

    return 0;
}
