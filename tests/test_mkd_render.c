/*
 * Tests for markdown render mode parsing functions.
 * These test the pure parsing logic used by mkd-render-mode.
 */

#include "testlib.h"
#include <stdint.h>

/* We test against the public API declared in mkd_render.h */
#include "../modes/mkd_render.h"

/*----------------------------------------------------------------------
 * Line classification
 *----------------------------------------------------------------------*/

static void fill_line(char32_t *buf, const char *s) {
    int i;
    for (i = 0; s[i]; i++)
        buf[i] = (unsigned char)s[i];
    buf[i] = 0;
}

TEST(classify, heading1) {
    char32_t buf[256];
    fill_line(buf, "# Hello");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HEADING);
}

TEST(classify, heading3) {
    char32_t buf[256];
    fill_line(buf, "### Subheading");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HEADING);
}

TEST(classify, heading_no_space) {
    char32_t buf[256];
    fill_line(buf, "#NoSpace");
    /* Not a valid heading per CommonMark - requires space after # */
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

TEST(classify, code_fence_backtick) {
    char32_t buf[256];
    fill_line(buf, "```python");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_CODE_FENCE);
}

TEST(classify, code_fence_tilde) {
    char32_t buf[256];
    fill_line(buf, "~~~");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_CODE_FENCE);
}

TEST(classify, blockquote) {
    char32_t buf[256];
    fill_line(buf, "> Some quoted text");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLOCKQUOTE);
}

TEST(classify, unordered_list_dash) {
    char32_t buf[256];
    fill_line(buf, "- item one");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, unordered_list_star) {
    char32_t buf[256];
    fill_line(buf, "* item two");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, unordered_list_plus) {
    char32_t buf[256];
    fill_line(buf, "+ item three");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, ordered_list) {
    char32_t buf[256];
    fill_line(buf, "1. First item");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, ordered_list_multidigit) {
    char32_t buf[256];
    fill_line(buf, "42. Forty-second");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, hr_dashes) {
    char32_t buf[256];
    fill_line(buf, "---");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_stars) {
    char32_t buf[256];
    fill_line(buf, "***");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_underscores) {
    char32_t buf[256];
    fill_line(buf, "___");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_with_spaces) {
    char32_t buf[256];
    fill_line(buf, "- - -");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, table) {
    char32_t buf[256];
    fill_line(buf, "| col1 | col2 |");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_TABLE);
}

TEST(classify, blank) {
    char32_t buf[256];
    fill_line(buf, "");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLANK);
}

TEST(classify, blank_spaces) {
    char32_t buf[256];
    fill_line(buf, "   ");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLANK);
}

TEST(classify, paragraph) {
    char32_t buf[256];
    fill_line(buf, "Just some normal text here.");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

TEST(classify, indented_list) {
    char32_t buf[256];
    fill_line(buf, "  - nested item");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

/*----------------------------------------------------------------------
 * Heading level extraction
 *----------------------------------------------------------------------*/

TEST(heading, level1) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "# Title");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 1);
    ASSERT_EQ(content_offset, 2);  /* skip "# " */
}

TEST(heading, level3) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "### Sub");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 3);
    ASSERT_EQ(content_offset, 4);  /* skip "### " */
}

TEST(heading, level6) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "###### Deep");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 6);
    ASSERT_EQ(content_offset, 7);
}

TEST(heading, not_heading) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "Not a heading");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 0);
}

/*----------------------------------------------------------------------
 * Bullet character selection
 *----------------------------------------------------------------------*/

TEST(bullet, depth0) {
    ASSERT_EQ(mkd_render_bullet_char(0, 0), 0x2022);  /* ● BULLET */
}

TEST(bullet, depth1) {
    ASSERT_EQ(mkd_render_bullet_char(1, 0), 0x25E6);  /* ◦ WHITE BULLET */
}

TEST(bullet, depth2) {
    ASSERT_EQ(mkd_render_bullet_char(2, 0), 0x2043);  /* ⁃ HYPHEN BULLET */
}

TEST(bullet, depth_wraps) {
    /* depth 3 should wrap around to depth 0's bullet */
    ASSERT_EQ(mkd_render_bullet_char(3, 0), 0x2022);
}

TEST(bullet, ordered) {
    /* ordered list items use the number itself, returns 0 to signal "use number" */
    ASSERT_EQ(mkd_render_bullet_char(0, 1), 0);
}

/*----------------------------------------------------------------------
 * Inline span detection
 *----------------------------------------------------------------------*/

TEST(inline_spans, bold_stars) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "hello **world** end");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_BOLD);
    ASSERT_EQ(spans[0].start, 6);   /* position of first * */
    ASSERT_EQ(spans[0].end, 15);    /* position after last * */
    ASSERT_EQ(spans[0].content_start, 8);  /* start of "world" */
    ASSERT_EQ(spans[0].content_end, 13);   /* end of "world" */
}

TEST(inline_spans, italic_stars) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "hello *world* end");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_ITALIC);
    ASSERT_EQ(spans[0].content_start, 7);
    ASSERT_EQ(spans[0].content_end, 12);
}

TEST(inline_spans, code_backtick) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "use `code` here");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_CODE);
    ASSERT_EQ(spans[0].content_start, 5);
    ASSERT_EQ(spans[0].content_end, 9);
}

TEST(inline_spans, link) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "click [here](http://example.com) now");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_LINK);
    ASSERT_EQ(spans[0].content_start, 7);   /* start of "here" */
    ASSERT_EQ(spans[0].content_end, 11);    /* end of "here" */
}

TEST(inline_spans, no_spans) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "plain text only");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_EQ(n, 0);
}

TEST(inline_spans, bold_underscores) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "hello __world__ end");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_BOLD);
    ASSERT_EQ(spans[0].content_start, 8);
    ASSERT_EQ(spans[0].content_end, 13);
}

TEST(inline_spans, italic_underscores) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "hello _world_ end");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_ITALIC);
    ASSERT_EQ(spans[0].content_start, 7);
    ASSERT_EQ(spans[0].content_end, 12);
}

TEST(inline_spans, multiple) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "**bold** and *italic*");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(spans[0].type, MKD_SPAN_BOLD);
    ASSERT_EQ(spans[1].type, MKD_SPAN_ITALIC);
}

TEST(inline_spans, strikethrough) {
    char32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "hello ~~deleted~~ end");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_TRUE(n >= 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_STRIKETHROUGH);
    ASSERT_EQ(spans[0].content_start, 8);
    ASSERT_EQ(spans[0].content_end, 15);
}

/*----------------------------------------------------------------------
 * Blockquote depth
 *----------------------------------------------------------------------*/

TEST(blockquote, depth1) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "> text");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 1);
    ASSERT_EQ(content_offset, 2);
}

TEST(blockquote, depth2) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "> > nested");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 2);
    ASSERT_EQ(content_offset, 4);
}

TEST(blockquote, not_quote) {
    char32_t buf[256];
    int content_offset;
    fill_line(buf, "plain");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 0);
}

/*----------------------------------------------------------------------
 * List item parsing
 *----------------------------------------------------------------------*/

TEST(list, unordered_dash) {
    char32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "- item");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 0);
    ASSERT_EQ(ordered, 0);
    ASSERT_EQ(content_offset, 2);
}

TEST(list, unordered_indented) {
    char32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "    - nested");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 1);
    ASSERT_EQ(ordered, 0);
    ASSERT_EQ(content_offset, 6);
}

TEST(list, ordered) {
    char32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "1. first");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 0);
    ASSERT_EQ(ordered, 1);
    ASSERT_EQ(content_offset, 3);
}

TEST(list, not_a_list) {
    char32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "normal text");
    ASSERT_FALSE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
}

int main() { return testlib_run_all(); }
