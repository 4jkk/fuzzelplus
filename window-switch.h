#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tllist.h>

#include "application.h"
#include "fdm.h"
#include "wayland.h"

struct window_switch;

struct window_switch_toplevel {
    char *identifier;
    char *title;
    char *app_id;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    bool done_received;
};

typedef tll(struct window_switch_toplevel) window_list_t;

struct window_switch {
    struct fdm *fdm;
    struct wayland *wayl;
    struct wl_display *display;
    struct wl_seat *seat;
    struct zwlr_foreign_toplevel_manager_v1 *manager;

    window_list_t windows;
    bool compositor_supports_protocol;
    bool finished;

    /* Format string for window display */
    char *format_string;
};

/* Initialize window switching functionality */
struct window_switch *window_switch_init(
    struct fdm *fdm, struct wayland *wayl,
    struct wl_display *display, struct wl_seat *seat);

/* Cleanup window switching resources */
void window_switch_destroy(struct window_switch *ws);

/* Check if compositor supports foreign toplevel management */
bool window_switch_check_compositor_support(struct window_switch *ws);

/* Convert window list to application list for display */
struct application_list *window_switch_to_applications(
    struct window_switch *ws, const char *window_format);

/* Activate selected window */
bool window_switch_activate(struct window_switch *ws, size_t window_index);

/* Get number of windows */
size_t window_switch_get_count(const struct window_switch *ws);

/* Registry global handler for binding the foreign toplevel manager */
void window_switch_registry_global(struct window_switch *ws,
    struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version);

