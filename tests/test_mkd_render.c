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

static void fill_line(uint32_t *buf, const char *s) {
    int i;
    for (i = 0; s[i]; i++)
        buf[i] = (unsigned char)s[i];
    buf[i] = 0;
}

TEST(classify, heading1) {
    uint32_t buf[256];
    fill_line(buf, "# Hello");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HEADING);
}

TEST(classify, heading3) {
    uint32_t buf[256];
    fill_line(buf, "### Subheading");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HEADING);
}

TEST(classify, heading_no_space) {
    uint32_t buf[256];
    fill_line(buf, "#NoSpace");
    /* Not a valid heading per CommonMark - requires space after # */
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

TEST(classify, code_fence_backtick) {
    uint32_t buf[256];
    fill_line(buf, "```python");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_CODE_FENCE);
}

TEST(classify, code_fence_tilde) {
    uint32_t buf[256];
    fill_line(buf, "~~~");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_CODE_FENCE);
}

TEST(classify, blockquote) {
    uint32_t buf[256];
    fill_line(buf, "> Some quoted text");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLOCKQUOTE);
}

TEST(classify, unordered_list_dash) {
    uint32_t buf[256];
    fill_line(buf, "- item one");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, unordered_list_star) {
    uint32_t buf[256];
    fill_line(buf, "* item two");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, unordered_list_plus) {
    uint32_t buf[256];
    fill_line(buf, "+ item three");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, ordered_list) {
    uint32_t buf[256];
    fill_line(buf, "1. First item");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, ordered_list_multidigit) {
    uint32_t buf[256];
    fill_line(buf, "42. Forty-second");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, hr_dashes) {
    uint32_t buf[256];
    fill_line(buf, "---");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_stars) {
    uint32_t buf[256];
    fill_line(buf, "***");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_underscores) {
    uint32_t buf[256];
    fill_line(buf, "___");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, hr_with_spaces) {
    uint32_t buf[256];
    fill_line(buf, "- - -");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_HR);
}

TEST(classify, table) {
    uint32_t buf[256];
    fill_line(buf, "| col1 | col2 |");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_TABLE);
}

TEST(classify, blank) {
    uint32_t buf[256];
    fill_line(buf, "");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLANK);
}

TEST(classify, blank_spaces) {
    uint32_t buf[256];
    fill_line(buf, "   ");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_BLANK);
}

TEST(classify, paragraph) {
    uint32_t buf[256];
    fill_line(buf, "Just some normal text here.");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

TEST(classify, indented_list) {
    uint32_t buf[256];
    fill_line(buf, "  - nested item");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

/*----------------------------------------------------------------------
 * Heading level extraction
 *----------------------------------------------------------------------*/

TEST(heading, level1) {
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "# Title");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 1);
    ASSERT_EQ(content_offset, 2);  /* skip "# " */
}

TEST(heading, level3) {
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "### Sub");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 3);
    ASSERT_EQ(content_offset, 4);  /* skip "### " */
}

TEST(heading, level6) {
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "###### Deep");
    ASSERT_EQ(mkd_render_heading_level(buf, &content_offset), 6);
    ASSERT_EQ(content_offset, 7);
}

TEST(heading, not_heading) {
    uint32_t buf[256];
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
    uint32_t buf[256];
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
    uint32_t buf[256];
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
    uint32_t buf[256];
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
    uint32_t buf[256];
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
    uint32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "plain text only");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_EQ(n, 0);
}

TEST(inline_spans, bold_underscores) {
    uint32_t buf[256];
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
    uint32_t buf[256];
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
    uint32_t buf[256];
    MkdRenderSpan spans[16];
    int n;
    fill_line(buf, "**bold** and *italic*");
    n = mkd_render_find_spans(buf, spans, 16);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(spans[0].type, MKD_SPAN_BOLD);
    ASSERT_EQ(spans[1].type, MKD_SPAN_ITALIC);
}

TEST(inline_spans, strikethrough) {
    uint32_t buf[256];
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
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "> text");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 1);
    ASSERT_EQ(content_offset, 2);
}

TEST(blockquote, depth2) {
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "> > nested");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 2);
    ASSERT_EQ(content_offset, 4);
}

TEST(blockquote, not_quote) {
    uint32_t buf[256];
    int content_offset;
    fill_line(buf, "plain");
    ASSERT_EQ(mkd_render_blockquote_depth(buf, &content_offset), 0);
}

/*----------------------------------------------------------------------
 * List item parsing
 *----------------------------------------------------------------------*/

TEST(list, unordered_dash) {
    uint32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "- item");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 0);
    ASSERT_EQ(ordered, 0);
    ASSERT_EQ(content_offset, 2);
}

TEST(list, unordered_indented) {
    uint32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "    - nested");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 1);
    ASSERT_EQ(ordered, 0);
    ASSERT_EQ(content_offset, 6);
}

TEST(list, ordered) {
    uint32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "1. first");
    ASSERT_TRUE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
    ASSERT_EQ(depth, 0);
    ASSERT_EQ(ordered, 1);
    ASSERT_EQ(content_offset, 3);
}

TEST(list, not_a_list) {
    uint32_t buf[256];
    int depth, ordered, content_offset;
    fill_line(buf, "normal text");
    ASSERT_FALSE(mkd_render_parse_list_item(buf, &depth, &ordered, &content_offset));
}

/*----------------------------------------------------------------------
 * Indented code blocks
 *----------------------------------------------------------------------*/

TEST(classify, indented_code_4spaces) {
    uint32_t buf[256];
    fill_line(buf, "    code here");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_INDENTED_CODE);
}

TEST(classify, indented_code_tab) {
    uint32_t buf[256];
    fill_line(buf, "\tcode here");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_INDENTED_CODE);
}

TEST(classify, indented_code_8spaces) {
    uint32_t buf[256];
    fill_line(buf, "        deeply indented");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_INDENTED_CODE);
}

TEST(classify, indented_list_not_code) {
    /* A nested list item at 4 spaces should be a list, not code */
    uint32_t buf[256];
    fill_line(buf, "    - nested item");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_LIST_ITEM);
}

TEST(classify, indented_3spaces_not_code) {
    /* 3 spaces is not enough for indented code */
    uint32_t buf[256];
    fill_line(buf, "   not code");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

/*----------------------------------------------------------------------
 * Table classification (requires two pipes)
 *----------------------------------------------------------------------*/

TEST(classify, table_with_pipes) {
    uint32_t buf[256];
    fill_line(buf, "| col1 | col2 |");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_TABLE);
}

TEST(classify, single_pipe_not_table) {
    /* A single pipe at start of line is not a table */
    uint32_t buf[256];
    fill_line(buf, "| only one pipe");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_PARAGRAPH);
}

TEST(classify, table_separator) {
    uint32_t buf[256];
    fill_line(buf, "|---|---|");
    ASSERT_EQ(mkd_render_classify_line(buf), MKD_LINE_TABLE);
}

/*----------------------------------------------------------------------
 * Improved inline span parsing (flanking rules)
 *----------------------------------------------------------------------*/

TEST(spans, star_mid_word_no_italic) {
    /* *foo *bar* — the first * is not left-flanking inside a word context,
     * but at start of line it is left-flanking. The key test is that
     * the span found is *bar* not *foo *bar* */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "foo*bar* baz");
    int n = mkd_render_find_spans(buf, spans, 8);
    /* foo*bar* — the * after foo is preceded by a word char,
     * so it should NOT be treated as an opening delimiter */
    ASSERT_EQ(n, 0);
}

TEST(spans, underscore_mid_word_no_emphasis) {
    /* underscores inside words should not trigger emphasis */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "foo_bar_baz");
    int n = mkd_render_find_spans(buf, spans, 8);
    ASSERT_EQ(n, 0);
}

TEST(spans, star_at_start_is_italic) {
    /* *foo* at start of line is valid italic */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "*foo* bar");
    int n = mkd_render_find_spans(buf, spans, 8);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_ITALIC);
    ASSERT_EQ(spans[0].content_start, 1);
    ASSERT_EQ(spans[0].content_end, 4);
}

TEST(spans, bold_not_followed_by_word) {
    /* **bold** followed by space is valid */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "**bold** rest");
    int n = mkd_render_find_spans(buf, spans, 8);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_BOLD);
}

TEST(spans, bold_followed_by_word_no_match) {
    /* **bold**word — closing ** followed by word char is not right-flanking */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "**bold**word");
    int n = mkd_render_find_spans(buf, spans, 8);
    ASSERT_EQ(n, 0);
}

TEST(spans, code_span_ignores_flanking) {
    /* backtick code spans don't use flanking rules */
    uint32_t buf[256];
    MkdRenderSpan spans[8];
    fill_line(buf, "foo`code`bar");
    int n = mkd_render_find_spans(buf, spans, 8);
    /* backtick doesn't use flanking, so this should match */
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MKD_SPAN_CODE);
}

int main() { return testlib_run_all(); }
