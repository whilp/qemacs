/*
 * Markdown render mode — public API for parsing functions.
 * Used by mkd_render.c (the mode) and by tests.
 *
 * Copyright (c) 2026 QEmacs contributors.
 * MIT License.
 */

#ifndef MKD_RENDER_H
#define MKD_RENDER_H

#include <stdint.h>

/* Line classification */
enum MkdLineType {
    MKD_LINE_BLANK,
    MKD_LINE_HEADING,
    MKD_LINE_PARAGRAPH,
    MKD_LINE_CODE_FENCE,
    MKD_LINE_INDENTED_CODE,
    MKD_LINE_BLOCKQUOTE,
    MKD_LINE_LIST_ITEM,
    MKD_LINE_HR,
    MKD_LINE_TABLE,
};

/* Inline span types */
enum MkdSpanType {
    MKD_SPAN_BOLD,
    MKD_SPAN_ITALIC,
    MKD_SPAN_CODE,
    MKD_SPAN_LINK,
    MKD_SPAN_STRIKETHROUGH,
};

typedef struct MkdRenderSpan {
    int type;           /* MkdSpanType */
    int start;          /* offset of opening marker in line */
    int end;            /* offset after closing marker (exclusive) */
    int content_start;  /* offset of first content char (after opening marker) */
    int content_end;    /* offset after last content char (exclusive, before closing marker) */
} MkdRenderSpan;

/*
 * Classify a line of text into a markdown block type.
 * `buf` is a NUL-terminated uint32_t line (no newline).
 */
int mkd_render_classify_line(const uint32_t *buf);

/*
 * Extract heading level (1-6) and offset of content text.
 * Returns 0 if not a heading.
 */
int mkd_render_heading_level(const uint32_t *buf, int *content_offset);

/*
 * Return the Unicode bullet character for a list item.
 * `depth` is nesting level (0 = top), `ordered` is nonzero for ordered lists.
 * Returns 0 for ordered lists (caller should render the number).
 */
uint32_t mkd_render_bullet_char(int depth, int ordered);

/*
 * Find inline spans (bold, italic, code, link, strikethrough) in a line.
 * Returns number of spans found, fills `spans` up to `max_spans`.
 */
int mkd_render_find_spans(const uint32_t *buf, MkdRenderSpan *spans, int max_spans);

/*
 * Determine blockquote nesting depth and content offset.
 * Returns depth (0 if not a blockquote).
 */
int mkd_render_blockquote_depth(const uint32_t *buf, int *content_offset);

/*
 * Parse a list item line.
 * Returns nonzero if this is a list item.
 * Sets *depth (0-based nesting), *ordered (0 or 1), *content_offset.
 */
int mkd_render_parse_list_item(const uint32_t *buf, int *depth, int *ordered, int *content_offset);

#endif /* MKD_RENDER_H */
