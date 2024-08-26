#include "dmenu.h"

#include <string.h>
#include <tllist.h>

#define LOG_MODULE "dmenu"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"
#include "event.h"
#include "getlines.h"

typedef tll(struct application *) dmenu_entries_list_t;

struct context {
    struct application_list *applications;
    dmenu_entries_list_t entries;
    size_t app_idx;
    int event_fd;
};

static bool
process_line(const char *line, size_t len, void *data)
{
    struct context *ctx = data;

    /*
     * Support Rofi’s extended dmenu protocol. One can specify an icon
     * by appending ‘\0icon\x1f<icon-name>’ to the entry:
     *
     *  “hello world\0icon\x1ffirefox”
     */
    char *icon_name = NULL;
    const char *extra = memchr(line, '\0', len);

    if (extra != NULL) {
        const size_t extra_len = line + len - extra;

        /*
         * 'extra' is "\0icon\x1f" - 6 characters. Require *more* than
         * 6, since icon *name* cannot be empty
         */
        if (extra_len > 6 &&  memcmp(extra, "\0icon\x1f", 6) == 0)
            icon_name = strndup(extra + 6, line + len - (extra + 6));
    }

    LOG_DBG("%s (icon=%s)", buffer, icon_name);

    char32_t *wline = ambstoc32(line);

    if (wline == NULL) {
        free(icon_name);
        return true;
    }

    char32_t *lowercase = c32dup(wline);
    for (size_t i = 0; i < c32len(lowercase); i++)
        lowercase[i] = toc32lower(lowercase[i]);

    struct application *app = malloc(sizeof(*app));
    *app = (struct application){
        .index = ctx->app_idx++,
        .title = wline,
        .title_lowercase = lowercase,
        .title_len = c32len(lowercase),
        .icon = {.name = icon_name},
        .visible = true,
    };

    tll_push_back(ctx->entries, app);
    return true;
}

static bool
chunk_done(void *data)
{
    struct context *ctx = data;
    struct application_list *applications = ctx->applications;

    if (ctx->event_fd == 0)
        return true;

    mtx_lock(&applications->lock);

    const size_t new_count = applications->count + tll_length(ctx->entries);
    applications->v = reallocarray(
        applications->v, new_count, sizeof(applications->v[0]));

    size_t i = applications->count;
    tll_foreach(ctx->entries, it) {
        applications->v[i++] = it->item;
        tll_remove(ctx->entries, it);
    }

    assert(i == new_count);
    applications->count = new_count;
    send_event(ctx->event_fd, EVENT_APPS_SOME_LOADED);
    mtx_unlock(&applications->lock);
    return true;
}

void
dmenu_load_entries(struct application_list *applications, char delim,
                   int event_fd, int abort_fd)
{
    struct context ctx = {
        .applications = applications,
        .entries = tll_init(),
        .app_idx = 0,
        .event_fd = event_fd,
    };

    if (getlines(delim, abort_fd, &process_line, &chunk_done, &ctx)) {
        mtx_lock(&applications->lock);

        const size_t new_count = applications->count + tll_length(ctx.entries);
        applications->v = reallocarray(
            applications->v, new_count, sizeof(applications->v[0]));

        size_t i = applications->count;
        tll_foreach(ctx.entries, it) {
            applications->v[i++] = it->item;
            tll_remove(ctx.entries, it);
        }

        applications->count = new_count;
        mtx_unlock(&applications->lock);
    }

    tll_free(ctx.entries);
}

bool
dmenu_execute(const struct application *app, ssize_t index,
              const struct prompt *prompt, enum dmenu_mode format)
{
    switch (format) {
    case DMENU_MODE_TEXT: {
        char *text = ac32tombs(app != NULL ? app->title : prompt_text(prompt));
        if (text != NULL)
            printf("%s\n", text);
        free(text);
        break;
    }

    case DMENU_MODE_INDEX:
        printf("%zd\n", index);
        break;
    }

    return true;
}
