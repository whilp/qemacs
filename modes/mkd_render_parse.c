/*
 * Markdown render mode — pure parsing functions.
 * No QEmacs dependencies. Testable standalone.
 *
 * Copyright (c) 2024 Charlie Gordon.
 * MIT License (see markdown.c header).
 */

#include "mkd_render.h"

/*----------------------------------------------------------------------
 * Pure parsing functions (no editor dependency — testable standalone)
 *----------------------------------------------------------------------*/

static int is_space(char32_t c) {
    return c == ' ' || c == '\t';
}

static int is_digit(char32_t c) {
    return c >= '0' && c <= '9';
}

int mkd_render_classify_line(const char32_t *buf)
{
    int i = 0;
    int indent = 0;
    char32_t c;

    /* skip leading spaces, count indent */
    while (is_space(buf[i])) {
        if (buf[i] == '\t')
            indent += 4;
        else
            indent++;
        i++;
    }

    c = buf[i];

    /* blank line */
    if (c == 0)
        return MKD_LINE_BLANK;

    /* heading: # followed by space */
    if (c == '#') {
        int j = i;
        while (buf[j] == '#') j++;
        if (is_space(buf[j]) && (j - i) <= 6)
            return MKD_LINE_HEADING;
    }

    /* code fence: ``` or ~~~ */
    if ((c == '`' && buf[i+1] == '`' && buf[i+2] == '`') ||
        (c == '~' && buf[i+1] == '~' && buf[i+2] == '~'))
        return MKD_LINE_CODE_FENCE;

    /* blockquote: > */
    if (c == '>')
        return MKD_LINE_BLOCKQUOTE;

    /* table: starts with | */
    if (c == '|')
        return MKD_LINE_TABLE;

    /* horizontal rule: 3+ of same char (- * _) with optional spaces */
    if (c == '-' || c == '*' || c == '_') {
        int count = 0;
        int j = i;
        char32_t rc = c;
        while (buf[j]) {
            if (buf[j] == rc)
                count++;
            else if (!is_space(buf[j]))
                break;
            j++;
        }
        if (buf[j] == 0 && count >= 3)
            return MKD_LINE_HR;
    }

    /* unordered list: - * + followed by space */
    if ((c == '-' || c == '*' || c == '+') && is_space(buf[i+1]))
        return MKD_LINE_LIST_ITEM;

    /* ordered list: digits followed by . and space */
    if (is_digit(c)) {
        int j = i;
        while (is_digit(buf[j])) j++;
        if (buf[j] == '.' && is_space(buf[j+1]))
            return MKD_LINE_LIST_ITEM;
    }

    return MKD_LINE_PARAGRAPH;
}

int mkd_render_heading_level(const char32_t *buf, int *content_offset)
{
    int i = 0, level;

    while (is_space(buf[i])) i++;

    if (buf[i] != '#') {
        *content_offset = 0;
        return 0;
    }

    level = 0;
    while (buf[i] == '#') {
        level++;
        i++;
    }

    if (!is_space(buf[i]) || level > 6) {
        *content_offset = 0;
        return 0;
    }

    /* skip the space after # */
    i++;
    *content_offset = i;
    return level;
}

static const char32_t bullet_chars[] = {
    0x2022,  /* ● BULLET (depth 0) */
    0x25E6,  /* ◦ WHITE BULLET (depth 1) */
    0x2043,  /* ⁃ HYPHEN BULLET (depth 2) */
};
#define NUM_BULLET_CHARS 3

char32_t mkd_render_bullet_char(int depth, int ordered)
{
    if (ordered)
        return 0;
    return bullet_chars[depth % NUM_BULLET_CHARS];
}

/*
 * Scan for a paired delimiter in `buf` starting at position `i`.
 * Returns position after the closing delimiter, or 0 if not found.
 */
static int scan_delimited(const char32_t *buf, int i,
                          const char *open, int open_len,
                          const char *close, int close_len)
{
    int j, k;

    /* match opening delimiter */
    for (j = 0; j < open_len; j++) {
        if (buf[i + j] != (unsigned char)open[j])
            return 0;
    }
    k = i + open_len;

    /* content must not start with space */
    if (is_space(buf[k]) || buf[k] == 0)
        return 0;

    /* scan for closing delimiter */
    while (buf[k]) {
        int match = 1;
        for (j = 0; j < close_len; j++) {
            if (buf[k + j] != (unsigned char)close[j]) {
                match = 0;
                break;
            }
        }
        if (match && !is_space(buf[k - 1]))
            return k + close_len;
        k++;
    }
    return 0;
}

int mkd_render_find_spans(const char32_t *buf, MkdRenderSpan *spans, int max_spans)
{
    int i = 0, n = 0;

    while (buf[i] && n < max_spans) {
        int end;
        MkdRenderSpan *sp = &spans[n];

        switch (buf[i]) {
        case '*':
            if (buf[i+1] == '*') {
                end = scan_delimited(buf, i, "**", 2, "**", 2);
                if (end) {
                    sp->type = MKD_SPAN_BOLD;
                    sp->start = i;
                    sp->end = end;
                    sp->content_start = i + 2;
                    sp->content_end = end - 2;
                    n++;
                    i = end;
                    continue;
                }
            }
            end = scan_delimited(buf, i, "*", 1, "*", 1);
            if (end) {
                sp->type = MKD_SPAN_ITALIC;
                sp->start = i;
                sp->end = end;
                sp->content_start = i + 1;
                sp->content_end = end - 1;
                n++;
                i = end;
                continue;
            }
            break;

        case '_':
            if (buf[i+1] == '_') {
                end = scan_delimited(buf, i, "__", 2, "__", 2);
                if (end) {
                    sp->type = MKD_SPAN_BOLD;
                    sp->start = i;
                    sp->end = end;
                    sp->content_start = i + 2;
                    sp->content_end = end - 2;
                    n++;
                    i = end;
                    continue;
                }
            }
            end = scan_delimited(buf, i, "_", 1, "_", 1);
            if (end) {
                sp->type = MKD_SPAN_ITALIC;
                sp->start = i;
                sp->end = end;
                sp->content_start = i + 1;
                sp->content_end = end - 1;
                n++;
                i = end;
                continue;
            }
            break;

        case '~':
            if (buf[i+1] == '~') {
                end = scan_delimited(buf, i, "~~", 2, "~~", 2);
                if (end) {
                    sp->type = MKD_SPAN_STRIKETHROUGH;
                    sp->start = i;
                    sp->end = end;
                    sp->content_start = i + 2;
                    sp->content_end = end - 2;
                    n++;
                    i = end;
                    continue;
                }
            }
            break;

        case '`':
            end = scan_delimited(buf, i, "`", 1, "`", 1);
            if (end) {
                sp->type = MKD_SPAN_CODE;
                sp->start = i;
                sp->end = end;
                sp->content_start = i + 1;
                sp->content_end = end - 1;
                n++;
                i = end;
                continue;
            }
            break;

        case '[':
            /* link: [text](url) */
            end = scan_delimited(buf, i, "[", 1, "]", 1);
            if (end && buf[end] == '(') {
                int url_end = end + 1;
                while (buf[url_end] && buf[url_end] != ')')
                    url_end++;
                if (buf[url_end] == ')') {
                    sp->type = MKD_SPAN_LINK;
                    sp->start = i;
                    sp->end = url_end + 1;
                    sp->content_start = i + 1;
                    sp->content_end = end - 1;
                    n++;
                    i = url_end + 1;
                    continue;
                }
            }
            break;
        }
        i++;
    }
    return n;
}

int mkd_render_blockquote_depth(const char32_t *buf, int *content_offset)
{
    int i = 0, depth = 0;

    while (buf[i] == '>') {
        depth++;
        i++;
        if (buf[i] == ' ')
            i++;
    }

    *content_offset = i;
    return depth;
}

int mkd_render_parse_list_item(const char32_t *buf, int *depth, int *ordered, int *content_offset)
{
    int i = 0, indent = 0;

    /* count leading indent */
    while (is_space(buf[i])) {
        if (buf[i] == '\t')
            indent += 4;
        else
            indent++;
        i++;
    }

    *depth = indent / 4;

    /* unordered: - * + followed by space */
    if ((buf[i] == '-' || buf[i] == '*' || buf[i] == '+') && is_space(buf[i+1])) {
        *ordered = 0;
        *content_offset = i + 2;
        return 1;
    }

    /* ordered: digits . space */
    if (is_digit(buf[i])) {
        int j = i;
        while (is_digit(buf[j])) j++;
        if (buf[j] == '.' && is_space(buf[j+1])) {
            *ordered = 1;
            *content_offset = j + 2;
            return 1;
        }
    }

    return 0;
}
