#include "getlines.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#define LOG_MODULE "getlines"
#define LOG_ENABLE_DBG 0
#include "log.h"


bool
getlines(char delim, int abort_fd,
         getlines_line_cb line_cb, getlines_chunk_done_cb chunk_done_cb,
         void *cb_data)
{
    size_t size = 0;
    size_t alloc_size = 16384;
    char *buffer = malloc(alloc_size);

    if (buffer == NULL) {
        LOG_ERRNO("failed to allocate input buffer");
        return false;
    }

    int flags;
    if ((flags = fcntl(STDIN_FILENO, F_GETFL)) < 0 ||
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOG_ERRNO("failed to set O_NONBLOCK on stdin");
        return false;
    }

    bool callback_aborted = false;
    errno = 0;

    while (!callback_aborted) {
        struct pollfd fds[] = {
            {.fd = STDIN_FILENO, .events = POLLIN},

            /* Must be last */
            {.fd = abort_fd, .events = POLLIN},
        };

        size_t count = sizeof(fds) / sizeof(fds[0]);
        if (abort_fd < 0)
            count--;

        int ret = poll(fds, count, -1);
        if (ret < 0) {
            LOG_ERRNO("failed to poll stdin");
            break;
        }

        if (fds[1].revents & (POLLIN | POLLHUP)) {
            LOG_WARN("aborted");
            break;
        }

        /* Increase size of input buffer, if necessary */
        if (size >= alloc_size - 2) {
            LOG_DBG("increasing input buffer size %zu -> %zu",
                    alloc_size, alloc_size * 2);

            alloc_size *= 2;

            char *new_buf = realloc(buffer, alloc_size);
            if (new_buf == NULL) {
                LOG_ERRNO("failed to reallocate input buffer");
                break;
            }

            buffer = new_buf;
        }

        ssize_t bytes_read = read(
            STDIN_FILENO, &buffer[size], alloc_size - size - 1);

        if (bytes_read < 0) {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN)
                break;
            LOG_ERRNO("failed to read from stdin");
            break;
        } else if (bytes_read == 0 && size == 0) {
            /* No more data on stdin, and all buffered data consumed */
            break;
        }

        size += bytes_read;

        /*
         * Consume as much as possible from the input buffer
         *
         * Input buffer may contain many lines, resulting in multiple
         * applications being added.
         *
         * Input buffer may not even contain a single line, in which
         * case we can't consume anything at all.
         *
         * Input buffer may contain one or more full lines, followed
         * by a final partial line. If stdin has been closed
         * (i.e. there's no more input to read), then treat the last,
         * partial line as a complete line (i.e. pretend it's followed
         * by a delimiter). But, if stdin is still open, don't consume
         * the last, partial line. Instead, wait for more input.
         */
        size_t consumed = 0;
        size_t left = size;

        while (left > 0) {
            char *line_start = &buffer[consumed];
            char *delim_at = memchr(line_start, delim, left);
            if (delim_at == NULL) {
                if (bytes_read > 0) {
                    /* No delimiter yet, wait for more data */
                    LOG_DBG("no delimiter found, waiting for more data...");
                    break;
                } else {
                    /* No delimiter, but stdin has been closed. Treat
                       the last chunk of data as a complete line. */
                    LOG_DBG("last line: pointing delim_at to the end of the buffer");
                    delim_at = &line_start[left];
                    size++;
                    left++;
                }
            }

            const size_t entry_len = delim_at - line_start;
            *delim_at = '\0';

            if (!line_cb(line_start, entry_len, cb_data)) {
                callback_aborted = true;
                break;
            }

            assert(entry_len + 1 <= left);
            consumed += entry_len + 1;
            left -= entry_len + 1;
        }

        chunk_done_cb(cb_data);

        memmove(buffer, &buffer[consumed], size - consumed);
        size -= consumed;
    }

    return !callback_aborted;
}
