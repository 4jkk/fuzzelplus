#include "pinentry.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>


#define LOG_MODULE "pinentry"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"
#include "event.h"
#include "getlines.h"
#include "prompt.h"
#include "util.h"
#include "version.h"

struct context {
    struct config *conf;
    struct prompt *prompt;
    int event_fd;
};

static char *
decode(const char *s)
{
    const size_t in_len = strlen(s);
    char *ret = calloc(in_len + 1, 1);

    for (size_t i = 0, out_i = 0; i < in_len; i++) {
        const char c = s[i];

        if (c == '%') {
            if (i + 2 >= in_len) {
                free(ret);
                return NULL;
            }

            ret[out_i++] = hex2nibble(s[i + 1]) << 4 | hex2nibble(s[i + 2]);
            i += 2;
        } else
            ret[out_i++] = c;
    }

    return ret;
}

static void
update_prompt(struct prompt *prompt, const char *encoded)
{
    char *decoded = decode(encoded);
    if (decoded == NULL)
        return;

    size_t count = mbstoc32(NULL, decoded, 0);
    if (count == (size_t)-1) {
        free(decoded);
        return;
    }

    char32_t new_prompt[count + 2];
    mbstoc32(new_prompt, decoded, count + 1);
    if (new_prompt[count - 1] == U':') {
        new_prompt[count - 1] = U'\0';
        prompt_set_placeholder(prompt, new_prompt);
        prompt_set_prompt(prompt, U"");
    } else {
        new_prompt[count] = U' ';
        new_prompt[count + 1] = U'\0';
        prompt_set_prompt(prompt, new_prompt);
    }

    free(decoded);
}

static bool
process_line(const char *line, size_t len, void *data)
{
    struct context *ctx = data;

    int event_fd = ctx->event_fd;

    static const char *const REPLY_OK = "OK";
    static const char *const REPLY_BYE = "OK closing connection";
    static const char *const REPLY_NOT_IMPLEMENTED = "ERR 536870981 Not implemented";
    static const char *const REPLY_PARAMETER_ERROR = "ERR 83886360 IPC parameter error";
    static const char *const REPLY_UNKNOWN_COMMAND = "ERR 536871187 Unknown IPC command";
    //static const char *const REPLY_UNKNOWN_OPTION = "ERR 83886254 Unknown option <Pinentry>";

    LOG_DBG("%s", line);

    if (strncasecmp(line, "option ", 7) == 0) {
        const char *option = line + 7;

        if (strncasecmp(option, "default-prompt=", 15) == 0) {
            update_prompt(ctx->prompt, &option[15]);
            puts(REPLY_OK);
        }

        /* Non-exhaustive list - just the ones I saw with gpg2+gpg-agent */
        else if (strcasecmp(option, "no-grab") == 0 ||
                 strcasecmp(option, "allow-external-password-cache") == 0 ||
                 strcasecmp(option, "allow-emacs-prompt") == 0 ||

                 strncasecmp(option, "ttyname=", 8) == 0 ||
                 strncasecmp(option, "ttytype=", 8) == 0 ||
                 strncasecmp(option, "lc-ctype=", 9) == 0 ||
                 strncasecmp(option, "lc-messages=", 12) == 0 ||
                 strncasecmp(option, "default-ok=", 11) == 0 ||
                 strncasecmp(option, "default-cancel=", 15) == 0 ||
                 strncasecmp(option, "default-yes=", 12) == 0 ||
                 strncasecmp(option, "default-no=", 11) == 0 ||
                 strncasecmp(option, "default-pwmngr=", 15) == 0 ||
                 strncasecmp(option, "default-cf-visi=", 16) == 0 ||
                 strncasecmp(option, "default-tt-visi=", 16) == 0 ||
                 strncasecmp(option, "default-tt-hide=", 16) == 0 ||
                 strncasecmp(option, "default-capshint=", 17) == 0 ||
                 strncasecmp(option, "touch-file=", 11) == 0 ||
                 strncasecmp(option, "owner=", 6) == 0)
        {
            LOG_DBG("ignoring: %s", line);
            //puts(REPLY_NOT_IMPLEMENTED);
            puts(REPLY_OK);
        }

        else {
            LOG_WARN("unrecognized: %s", line);
            //puts(REPLY_UNKNOWN_OPTION);
            puts(REPLY_OK);
        }
    }

    else if (strncasecmp(line, "getinfo ", 8) == 0) {
        const char *info = line + 8;

        if (strcmp(info, "flavor") == 0) {
            puts("D fuzzel");
            puts(REPLY_OK);
        }

        else if (strcmp(info, "version") == 0) {
            printf("D %s\n", FUZZEL_VERSION);
            puts(REPLY_OK);
        }

        else if (strcmp(info, "pid") == 0) {
            printf("D %u\n", getpid());
            puts(REPLY_OK);
        }

        else if (strcmp(info, "ttyinfo") == 0) {
            puts(REPLY_NOT_IMPLEMENTED);
        }

        else {
            LOG_WARN("unrecognized GETINFO command: %s", line);
            puts(REPLY_PARAMETER_ERROR);
        }
    }

    else if (strncasecmp(line, "setkeyinfo ", 11) == 0) {
        puts(REPLY_OK);
    }

    else if (strncasecmp(line, "settitle ", 9) == 0 ||
             strncasecmp(line, "setdesc ", 8) == 0 ||
             strncasecmp(line, "setok ", 6) == 0 ||
             strncasecmp(line, "setcancel ", 10) == 0 ||
             strncasecmp(line, "setnotok ", 9) == 0 ||
             strncasecmp(line, "seterror ", 9) == 0)
    {
        //char *decoded = decode(...);
        //free(decoded);
        LOG_DBG("ignoring: %s", line);
        puts(REPLY_OK);
    }

    else if (strncasecmp(line, "setprompt ", 10) == 0) {
        update_prompt(ctx->prompt, &line[10]);
        puts(REPLY_OK);
    }

    else if (strcasecmp(line, "getpin") == 0) {
        static bool getpin_already_used = false;

        if (getpin_already_used) {
            /* We don't support opening the window multiple times */
            puts(REPLY_NOT_IMPLEMENTED);
        } else {
            send_event(event_fd, EVENT_APPS_ALL_LOADED);
            getpin_already_used = true;
            /* Reply sent by pinentry_execute() */
        }
    }

    else if (strcasecmp(line, "reset") == 0) {
        prompt_set_prompt(ctx->prompt, ctx->conf->prompt);
        prompt_set_placeholder(ctx->prompt, ctx->conf->placeholder);
        puts(REPLY_OK);
    }

    else if (strcasecmp(line, "bye") == 0) {
        puts(REPLY_BYE);
        close(STDIN_FILENO);
        return false;
    }

    else if (strcasecmp(line, "nop") == 0) {
        puts(REPLY_OK);
    }

    else if (strcasecmp(line, "help") == 0) {
        puts("# BYE");
        puts("# NOP");
        puts("# OPTION");
        puts("# GETINFO");
        puts("# SETPROMPT");
        puts("# GETPIN");
        puts("# HELP");
    }

    else if (strcasecmp(line, "confirm") == 0 ||
             strncasecmp(line, "cancel", 6) == 0 ||
             strncasecmp(line, "setgenpin", 9) == 0 ||
             strncasecmp(line, "setgenpin_tt", 12) == 0 ||
             strncasecmp(line, "settimeout ", 11) == 0 ||
             strncasecmp(line, "end", 3) == 0 ||
             strncasecmp(line, "quit", 4) == 0 ||
             strncasecmp(line, "auth", 4) == 0 ||
             strncasecmp(line, "clearpassphrase", 15) == 0 ||
             strncasecmp(line, "setrepeat", 9) == 0 ||
             //strncasecmp(line, "setrepeaterror", 15) == 0 ||
             strncasecmp(line, "setqualitybar", 13) == 0
             //strncasecmp(line, "setqualitybar_tt ", 11) == 0
             )
    {
        LOG_WARN("not implemented: %s", line);
        puts(REPLY_NOT_IMPLEMENTED);
    }
             
    else {
        LOG_WARN("unrecognized: %s", line);
        puts(REPLY_UNKNOWN_COMMAND);
    }

    fflush(stdout);
    return true;
}

static bool
chunk_done(void *data)
{
    return true;
}

bool
pinentry_read_commands(struct config *conf, struct prompt *prompt,
                       int event_fd, int abort_fd)
{
    printf("OK fuzzel is pleased to meet you\n");
    fflush(stdout);

    struct context ctx = {
        .conf = conf,
        .prompt = prompt,
        .event_fd = event_fd,
    };

    return getlines('\n', abort_fd, &process_line, &chunk_done, &ctx);
}

bool
pinentry_execute(const struct prompt *prompt)
{
    const char32_t *secret = prompt_text(prompt);
    if (secret[0] != U'\0') {
        printf("D %ls\n", (const wchar_t *)prompt_text(prompt));
        printf("END\n");
    }
    printf("OK\n");
    fflush(stdout);
    return true;
}

bool
pinentry_cancel(void)
{
    printf("ERR 83886179 Operation cancelled\n");
    fflush(stdout);
    return true;
}
