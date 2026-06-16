#include "prompt.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define LOG_MODULE "prompt"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"
#include "wayland.h"
#include "xmalloc.h"

struct prompt {
    char32_t *prompt;
    char32_t *placeholder;
    char32_t *text;
    size_t cursor;
};

struct prompt *
prompt_init(const char32_t *prompt_text, const char32_t *placeholder,
            const char32_t *text, const bool center_prompt, unsigned int chars)
{
    const bool have_initial_text = text != NULL && text[0] != U'\0';

    struct prompt *prompt = xmalloc(sizeof(*prompt));
    *prompt = (struct prompt) {
        .prompt = center_prompt? prompt_center(prompt_text, chars) : xc32dup(prompt_text),
        .placeholder = xc32dup(placeholder),
        .text = have_initial_text ? xc32dup(text) : xcalloc(1, sizeof(char32_t)),
        .cursor = have_initial_text ? c32len(text) : 0,
    };

    return prompt;
}

char32_t* prompt_center(const char32_t *prompt_text, unsigned int chars){

	unsigned int len =0;
	for(; prompt_text[len]!='\0' && len<= chars ; len++);

	int padding = chars - len;
	padding = ( padding % 2 ? padding - 1 : padding )/2;

	char32_t buf[chars + 4]; // two characters are for preventing cursor rendering, the last is for null character

	int i =0, j=0;
	for(; i < padding; i++ ) buf[i]=' ';
	for(; j <len;				   ) buf[i++]=prompt_text[j++];
	for(; i <= chars+2; i++) buf[i]=' ';
	buf[chars + 3]= '\0';

	return xc32dup(buf);
}

void
prompt_destroy(struct prompt *prompt)
{
    if (prompt == NULL)
        return;

    free(prompt->prompt);
    free(prompt->placeholder);
    free(prompt->text);
    free(prompt);
}

void
prompt_insert_chars(struct prompt *prompt, const char *text, size_t len)
{
    size_t wlen = mbsntoc32(NULL, text, len, 0);

    const size_t new_len = c32len(prompt->text) + wlen + 1;
    char32_t *new_text = xreallocarray(prompt->text, new_len, sizeof(char32_t));

    memmove(&new_text[prompt->cursor + wlen],
            &new_text[prompt->cursor],
            (c32len(new_text) - prompt->cursor + 1) * sizeof(char32_t));

    mbsntoc32(&new_text[prompt->cursor], text, len, wlen + 1);

    prompt->text = new_text;
    prompt->cursor += wlen;
}

const char32_t *
prompt_prompt(const struct prompt *prompt)
{
    return prompt->prompt;
}

const char32_t *
prompt_placeholder(const struct prompt *prompt)
{
    return prompt->placeholder;
}

const char32_t *
prompt_text(const struct prompt *prompt)
{
    return prompt->text;
}

size_t
prompt_cursor(const struct prompt *prompt)
{
    return prompt->cursor;
}

static size_t
idx_next_char(struct prompt *prompt)
{
    if (prompt->text[prompt->cursor] == U'\0')
        return prompt->cursor;
    return prompt->cursor + 1;
}

static size_t
idx_prev_char(const struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return 0;

    return prompt->cursor - 1;
}

static size_t
idx_prev_word(const struct prompt *prompt)
{
    size_t prev_char = idx_prev_char(prompt);
    const char32_t *space = &prompt->text[prev_char];

    /* Ignore initial spaces */
    while (space >= prompt->text && iswspace(*space))
        space--;

    /* Skip non-spaces */
    while (space >= prompt->text && !iswspace(*space))
        space--;

    return space - prompt->text + 1;
}

static size_t
idx_next_word(const struct prompt *prompt)
{
    const char32_t *end = prompt->text + c32len(prompt->text);
    const char32_t *space = &prompt->text[prompt->cursor];

    /* Ignore initial non-spaces */
    while (space < end && !iswspace(*space))
        space++;

    /* Skip spaces */
    while (space < end && iswspace(*space))
        space++;

    return space - prompt->text;
}

bool
prompt_cursor_home(struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return false;

    prompt->cursor = 0;
    return true;
}

bool
prompt_cursor_end(struct prompt *prompt)
{
    size_t text_len = c32len(prompt->text);
    if (prompt->cursor >= text_len)
        return false;

    prompt->cursor = text_len;
    return true;
}

bool
prompt_cursor_next_char(struct prompt *prompt)
{
    size_t idx = idx_next_char(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_prev_char(struct prompt *prompt)
{
    size_t idx = idx_prev_char(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_prev_word(struct prompt *prompt)
{
    size_t idx = idx_prev_word(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_next_word(struct prompt *prompt)
{
    size_t idx = idx_next_word(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_erase_all(struct prompt *prompt)
{
    free(prompt->text);
    prompt->text = xcalloc(1, sizeof(char32_t));
    prompt->cursor = 0;
    return true;
}

bool
prompt_erase_next_char(struct prompt *prompt)
{
    if (prompt->cursor >= c32len(prompt->text))
        return false;

    size_t next_char = idx_next_char(prompt);
    memmove(&prompt->text[prompt->cursor],
            &prompt->text[next_char],
            (c32len(prompt->text) - next_char + 1) * sizeof(char32_t));
    return true;
}

bool
prompt_erase_prev_char(struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return false;

    size_t prev_char = idx_prev_char(prompt);
    memmove(&prompt->text[prev_char],
            &prompt->text[prompt->cursor],
            (c32len(prompt->text) - prompt->cursor + 1) * sizeof(char32_t));
    prompt->cursor = prev_char;
    return true;
}

bool
prompt_erase_next_word(struct prompt *prompt)
{
    size_t next_word = idx_next_word(prompt);
    if (next_word == prompt->cursor)
        return false;

    memmove(&prompt->text[prompt->cursor],
            &prompt->text[next_word],
            (c32len(prompt->text) - next_word + 1) * sizeof(char32_t));
    return true;
}

bool
prompt_erase_prev_word(struct prompt *prompt)
{
    size_t new_cursor = idx_prev_word(prompt);
    if (new_cursor == prompt->cursor)
        return false;

    memmove(&prompt->text[new_cursor],
            &prompt->text[prompt->cursor],
            (c32len(prompt->text) - prompt->cursor + 1) * sizeof(char32_t));
    prompt->cursor = new_cursor;
    return true;
}

bool
prompt_erase_after_cursor(struct prompt *prompt)
{
    prompt->text[prompt->cursor] = U'\0';
    return true;
}

bool
prompt_erase_before_cursor(struct prompt *prompt)
{
    if (prompt->cursor == 0) {
        return false;
    }

    memmove(&prompt->text[0],
            &prompt->text[prompt->cursor],
            (c32len(prompt->text) - prompt->cursor + 1) * sizeof(char32_t));
    prompt->cursor = 0;

    return true;
}

