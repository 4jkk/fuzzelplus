#pragma once

#include <stdbool.h>

#include "config.h"
#include "prompt.h"

bool pinentry_read_commands(struct config *conf, struct prompt *prompt,
                            int event_fd, int abort_fd);
bool pinentry_execute(const struct prompt *prompt);
bool pinentry_cancel(void);
