#include "window-switch.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wlr-foreign-toplevel-management-unstable-v1.h>

#define LOG_MODULE "window-switch"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "application.h"
#include "char32.h"
#include "xdg.h"
#include "xmalloc.h"
#include "xsnprintf.h"

/* Protocol event handlers */
static void handle_manager_toplevel(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *manager,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel);

static void handle_manager_finished(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *manager);

static void handle_toplevel_title(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    const char *title);

static void handle_toplevel_app_id(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    const char *app_id);

static void handle_toplevel_output_enter(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_output *output);

static void handle_toplevel_output_leave(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_output *output);

static void handle_toplevel_state(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_array *state);

static void handle_toplevel_done(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel);

static void handle_toplevel_closed(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel);

static void handle_toplevel_parent(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct zwlr_foreign_toplevel_handle_v1 *parent);

/* Protocol listener structs */
static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = handle_toplevel_title,
    .app_id = handle_toplevel_app_id,
    .output_enter = handle_toplevel_output_enter,
    .output_leave = handle_toplevel_output_leave,
    .state = handle_toplevel_state,
    .done = handle_toplevel_done,
    .closed = handle_toplevel_closed,
    .parent = handle_toplevel_parent,
};

/* Helper function to find toplevel by handle */
static struct window_switch_toplevel *
find_toplevel_by_handle(struct window_switch *ws,
    struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    tll_foreach(ws->windows, it) {
        if (it->item.handle == handle) {
            return &it->item;
        }
    }
    return NULL;
}

/* Protocol event implementations */
static void
handle_manager_toplevel(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *manager,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    struct window_switch *ws = data;

    LOG_DBG("new toplevel handle received");

    struct window_switch_toplevel new_toplevel = {
        .identifier = NULL,
        .title = NULL,
        .app_id = NULL,
        .handle = toplevel,
        .done_received = false,
    };

    tll_push_back(ws->windows, new_toplevel);

    zwlr_foreign_toplevel_handle_v1_add_listener(
        toplevel, &toplevel_listener, ws);
}

static void
handle_manager_finished(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *manager)
{
    struct window_switch *ws = data;

    LOG_DBG("manager finished");
    ws->finished = true;
}

static void
handle_toplevel_title(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    const char *title)
{
    struct window_switch *ws = data;
    struct window_switch_toplevel *tl = find_toplevel_by_handle(ws, toplevel);

    if (tl) {
        free(tl->title);
        tl->title = title ? xstrdup(title) : NULL;
        LOG_DBG("toplevel title: %s", title ? title : "(null)");
    }
}

static void
handle_toplevel_app_id(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    const char *app_id)
{
    struct window_switch *ws = data;
    struct window_switch_toplevel *tl = find_toplevel_by_handle(ws, toplevel);

    if (tl) {
        free(tl->app_id);
        tl->app_id = app_id ? xstrdup(app_id) : NULL;
        LOG_DBG("toplevel app_id: %s", app_id ? app_id : "(null)");
    }
}

static void
handle_toplevel_output_enter(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_output *output)
{
    /* We don't need to track outputs for window switching */
    LOG_DBG("toplevel entered output");
}

static void
handle_toplevel_output_leave(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_output *output)
{
    /* We don't need to track outputs for window switching */
    LOG_DBG("toplevel left output");
}

static void
handle_toplevel_state(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct wl_array *state)
{
    /* We don't need to track state for window switching */
    LOG_DBG("toplevel state changed");
}

static void
handle_toplevel_done(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    struct window_switch *ws = data;
    struct window_switch_toplevel *tl = find_toplevel_by_handle(ws, toplevel);

    if (tl) {
        tl->done_received = true;
        LOG_DBG("toplevel done: title=%s, app_id=%s",
                tl->title ? tl->title : "(null)",
                tl->app_id ? tl->app_id : "(null)");
    }
}

static void
handle_toplevel_closed(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    struct window_switch *ws = data;

    LOG_DBG("toplevel closed");

    tll_foreach(ws->windows, it) {
        if (it->item.handle == toplevel) {
            free(it->item.identifier);
            free(it->item.title);
            free(it->item.app_id);
            zwlr_foreign_toplevel_handle_v1_destroy(it->item.handle);
            tll_remove(ws->windows, it);
            break;
        }
    }
}

static void
handle_toplevel_parent(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel,
    struct zwlr_foreign_toplevel_handle_v1 *parent)
{
    /* We don't need to track parent relationships for window switching */
    LOG_DBG("toplevel parent changed");
}

/* Public API implementations */
struct window_switch *
window_switch_init(struct fdm *fdm, struct wayland *wayl,
    struct wl_display *display, struct wl_seat *seat)
{
    struct window_switch *ws = xcalloc(1, sizeof(struct window_switch));

    ws->fdm = fdm;
    ws->wayl = wayl;
    ws->display = display;
    ws->seat = seat;
    ws->windows = (window_list_t)tll_init();
    ws->compositor_supports_protocol = false;
    ws->finished = false;
    ws->format_string = NULL;

    return ws;
}

void
window_switch_destroy(struct window_switch *ws)
{
    if (!ws)
        return;

    /* Clean up all toplevel handles */
    tll_foreach(ws->windows, it) {
        free(it->item.identifier);
        free(it->item.title);
        free(it->item.app_id);
        if (it->item.handle) {
            zwlr_foreign_toplevel_handle_v1_destroy(it->item.handle);
        }
    }
    tll_free(ws->windows);

    /* Clean up manager */
    if (ws->manager) {
        zwlr_foreign_toplevel_manager_v1_stop(ws->manager);
        zwlr_foreign_toplevel_manager_v1_destroy(ws->manager);
    }

    free(ws->format_string);
    free(ws);
}

bool
window_switch_check_compositor_support(struct window_switch *ws)
{
    /* This will be called after registry binding - check if manager was bound */
    return ws->compositor_supports_protocol;
}


/* Helper function to find the matching closing brace, handling nested braces */
static const char *
find_matching_brace(const char *start)
{
    int brace_count = 1;
    const char *pos = start;

    while (*pos && brace_count > 0) {
        if (*pos == '{') {
            brace_count++;
        } else if (*pos == '}') {
            brace_count--;
        }
        if (brace_count > 0) {
            pos++;
        }
    }

    return brace_count == 0 ? pos : NULL;
}

static char *
format_window_entry(const struct window_switch_toplevel *toplevel,
    const char *format)
{
    /* Basic input validation */
    if (!toplevel) {
        return xstrdup("");
    }

    const char *app_id = toplevel->app_id ? toplevel->app_id : "";
    const char *title = toplevel->title ? toplevel->title : "";

    /* Use default format if none provided */
    if (!format || strlen(format) == 0) {
        format = "{app_id:{:<15.15}} {title:{:<55.55}...}";
    }

    char *result = xstrdup("");
    const char *pos = format;

    while (*pos) {
        if (*pos == '{') {
            const char *field_start = pos + 1;
            const char *field_end = find_matching_brace(pos + 1);

            if (!field_end) {
                /* Malformed format string, just add the '{' and continue */
                char *new_result = xasprintf("%s{", result);
                free(result);
                result = new_result;
                pos++;
                continue;
            }

            /* Extract the complete field specification */
            size_t field_spec_len = field_end - field_start;
            char field_spec[field_spec_len + 1];
            strncpy(field_spec, field_start, field_spec_len);
            field_spec[field_spec_len] = '\0';

            /* Parse field name and format specification */
            char *colon = strchr(field_spec, ':');
            const char *field_name = field_spec;
            const char *field_value = NULL;
            int width = -1;
            int precision = -1;
            bool add_ellipsis = false;
            char align_char = '<'; /* Default to left align */

            if (colon) {
                *colon = '\0'; /* Split field name from format spec */
                char *format_spec = colon + 1;

                /* Check for ellipsis after the closing brace */
                if (field_end[1] == '.' && field_end[2] == '.' && field_end[3] == '.') {
                    add_ellipsis = true;
                }

                /* Handle nested braces in format spec: {app_id:{:^10.10}} */
                /* Skip outer braces if present */
                if (format_spec[0] == '{' && format_spec[strlen(format_spec) - 1] == '}') {
                    format_spec[strlen(format_spec) - 1] = '\0'; /* Remove closing brace */
                    format_spec++; /* Skip opening brace */
                }

                /* Parse format spec like :<NNN.NNN, :>NNN.NNN, or :^NNN.NNN */
                /* Extract width and precision with alignment from format like :<NNN.NNN, :>NNN.NNN, :^NNN.NNN */
                if (format_spec[0] == ':' && format_spec[1] && (format_spec[1] == '<' || format_spec[1] == '>' || format_spec[1] == '^')) {
                    align_char = format_spec[1];
                    if (sscanf(format_spec + 2, "%d.%d", &width, &precision) != 2) {
                        /* Try just width */
                        sscanf(format_spec + 2, "%d", &width);
                    }
                } else if (format_spec[0] == ':') {
                    /* Default left alignment */
                    if (sscanf(format_spec + 1, "%d.%d", &width, &precision) != 2) {
                        /* Try just width */
                        if (sscanf(format_spec + 1, "%d", &width) != 1) {
                            /* Try precision only (.NNN) */
                            if (format_spec[1] == '.' && sscanf(format_spec + 2, "%d", &precision) == 1) {
                                width = -1; /* No width specified */
                            }
                        }
                    }
                }
            }

            /* Get field value */
            if (strcmp(field_name, "app_id") == 0) {
                field_value = app_id;
            } else if (strcmp(field_name, "title") == 0) {
                field_value = title;
            } else {
                field_value = ""; /* Unknown field */
            }

            /* Format the field value */
            char formatted_field[512] = {0};
            size_t field_value_len = strlen(field_value);

            if (precision > 0 && field_value_len > (size_t)precision) {
                /* Truncate and possibly add ellipsis */
                if (add_ellipsis && precision > 3) {
                    snprintf(formatted_field, sizeof(formatted_field), "%.*s...", precision - 3, field_value);
                } else {
                    snprintf(formatted_field, sizeof(formatted_field), "%.*s", precision, field_value);
                }
            } else {
                strcpy(formatted_field, field_value);
            }

            /* Apply width padding with alignment if specified */
            if (width > 0 && strlen(formatted_field) < (size_t)width) {
                char padded_field[512] = {0};

                /* Ensure width is reasonable to prevent buffer overflow */
                int safe_width = width;
                int max_safe_width = sizeof(padded_field) - strlen(formatted_field) - 1;
                if (safe_width > max_safe_width) {
                    safe_width = max_safe_width;
                }

                switch (align_char) {
                case '<': /* Left align (default) */
                    if (safe_width > 0) {
                        snprintf(padded_field, sizeof(padded_field), "%-*s", safe_width, formatted_field);
                    } else {
                        strcpy(padded_field, formatted_field);
                    }
                    break;
                case '>': /* Right align */
                    if (safe_width > 0) {
                        snprintf(padded_field, sizeof(padded_field), "%*s", safe_width, formatted_field);
                    } else {
                        strcpy(padded_field, formatted_field);
                    }
                    break;
                case '^': /* Center align */
                    {
                        size_t field_len = strlen(formatted_field);
                        size_t max_total = sizeof(padded_field) - 1; /* Reserve space for null terminator */

                        if (field_len >= max_total || safe_width <= 0 || field_len >= (size_t)safe_width) {
                            /* Field is too long or width is invalid, just copy it */
                            strcpy(padded_field, formatted_field);
                        } else {
                            int padding = safe_width - field_len;
                            int left_pad = padding / 2;
                            int right_pad = padding - left_pad;

                            /* Manually build the padded string to avoid format truncation warning */
                            size_t pos = 0;

                            /* Add left padding */
                            for (int i = 0; i < left_pad && pos < max_total; i++) {
                                padded_field[pos++] = ' ';
                            }

                            /* Add the field content */
                            for (size_t i = 0; i < field_len && pos < max_total; i++) {
                                padded_field[pos++] = formatted_field[i];
                            }

                            /* Add right padding */
                            for (int i = 0; i < right_pad && pos < max_total; i++) {
                                padded_field[pos++] = ' ';
                            }

                            padded_field[pos] = '\0';
                        }
                    }
                    break;
                default:
                    /* Fallback to left align */
                    snprintf(padded_field, sizeof(padded_field), "%-*s", width, formatted_field);
                    break;
                }

                strcpy(formatted_field, padded_field);
            }

            /* Append formatted field to result */
            char *new_result = xasprintf("%s%s", result, formatted_field);
            free(result);
            result = new_result;

            /* Skip past the field and any trailing ellipsis */
            pos = field_end + 1;
            if (add_ellipsis && pos[0] == '.' && pos[1] == '.' && pos[2] == '.') {
                pos += 3;
            }
        } else {
            /* Regular character, just append it */
            char *new_result = xasprintf("%s%c", result, *pos);
            free(result);
            result = new_result;
            pos++;
        }
    }

    return result;
}

struct application_list *
window_switch_to_applications(struct window_switch *ws, const char *window_format)
{
    struct application_list *apps = applications_init();
    if (!apps)
        return NULL;

    size_t window_count = 0;

    /* Count valid windows first */
    tll_foreach(ws->windows, it) {
        if (it->item.done_received)
            window_count++;
    }

    if (window_count == 0)
        return apps;

    /* Allocate array for applications */
    mtx_lock(&apps->lock);
    apps->v = xmalloc(window_count * sizeof(apps->v[0]));

    size_t app_index = 0;
    size_t window_index = 0; /* Track position in all windows list */

    tll_foreach(ws->windows, it) {
        struct window_switch_toplevel *toplevel = &it->item;

        /* Only include windows that have received their initial data */
        if (!toplevel->done_received) {
            window_index++;
            continue;
        }

        /* Create an application entry for this window */
        struct application *app = xcalloc(1, sizeof(struct application));

        /* Use app_id as the ID for icon lookup */
        app->id = toplevel->app_id ? xstrdup(toplevel->app_id) : NULL;

        /* Set index to map back to window position */
        app->index = window_index;

        /* Format the title using window_format */
        char *formatted_title = format_window_entry(toplevel, window_format);
        if (formatted_title && strlen(formatted_title) > 0) {
            app->title = ambstoc32(formatted_title);
        } else {
            /* Fallback to empty string if format failed */
            app->title = ambstoc32("");
        }
        free(formatted_title);

        /* Set fields needed for matching */
        app->visible = true;
        app->count = 0;

        /* Set basename to lowercase app_id for MATCH_FILENAME matching */
        if (toplevel->app_id) {
            char32_t *app_id_c32 = ambstoc32(toplevel->app_id);
            if (app_id_c32) {
                app->basename = xc32dup(app_id_c32);
                app->basename_len = c32len(app->basename);
                for (size_t i = 0; i < app->basename_len; i++)
                    app->basename[i] = toc32lower(app->basename[i]);
                free(app_id_c32);
            }
        }

        /* Set title_lowercase for MATCH_NAME matching */
        if (app->title) {
            app->title_lowercase = xc32dup(app->title);
            app->title_len = c32len(app->title_lowercase);
            for (size_t i = 0; i < app->title_len; i++)
                app->title_lowercase[i] = toc32lower(app->title_lowercase[i]);
        }

        /* Initialize icon using desktop file lookup first, fallback to app_id */
        char *icon_name = xdg_lookup_icon_name_for_app_id(toplevel->app_id);
        if (!icon_name && toplevel->app_id) {
            /* Fallback to app_id if no desktop file found */
            icon_name = xstrdup(toplevel->app_id);
        }
        app->icon.name = icon_name;
        app->icon.type = ICON_NONE; /* ICON_NONE until loaded */

        /* Add to applications array */
        apps->v[app_index] = app;
        app_index++;
        window_index++;
    }

    apps->count = window_count;
    apps->visible_count = window_count;
    mtx_unlock(&apps->lock);

    return apps;
}

bool
window_switch_activate(struct window_switch *ws, size_t window_index)
{
    if (window_index >= tll_length(ws->windows)) {
        LOG_ERR("window index %zu out of range", window_index);
        return false;
    }

    size_t idx = 0;
    tll_foreach(ws->windows, it) {
        if (idx == window_index) {
            struct window_switch_toplevel *toplevel = &it->item;

            if (!toplevel->handle) {
                LOG_ERR("toplevel handle is null");
                return false;
            }

            if (!ws->seat) {
                LOG_ERR("no seat available for activation");
                return false;
            }

            LOG_DBG("activating window: %s",
                    toplevel->title ? toplevel->title : "(untitled)");

            zwlr_foreign_toplevel_handle_v1_activate(toplevel->handle, ws->seat);
            wl_display_flush(ws->display);

            return true;
        }
        idx++;
    }

    return false;
}

size_t
window_switch_get_count(const struct window_switch *ws)
{
    return tll_length(ws->windows);
}

/* Registry global handler to bind foreign toplevel manager */
void
window_switch_registry_global(struct window_switch *ws,
    struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        LOG_DBG("binding foreign toplevel manager");

        ws->manager = wl_registry_bind(registry, name,
            &zwlr_foreign_toplevel_manager_v1_interface,
            version < 3 ? version : 3);

        if (ws->manager) {
            ws->compositor_supports_protocol = true;
            zwlr_foreign_toplevel_manager_v1_add_listener(
                ws->manager, &manager_listener, ws);
        }
    }
}

