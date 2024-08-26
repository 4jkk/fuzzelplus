#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*getlines_line_cb)(const char *line, size_t len, void *data);
typedef bool (*getlines_chunk_done_cb)(void *data);

bool getlines(char delim, int abort_fd,
              getlines_line_cb line_cb, getlines_chunk_done_cb chunk_done_cb,
              void *cb_data);
