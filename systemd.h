#pragma once

#include <stdbool.h>
#include <unistd.h>

bool put_pid_in_scope(pid_t pid, const char *name);
