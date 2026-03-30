/*
 * Tests for split window geometry and wrap_cols behavior.
 *
 * Reproduces the grey bar bug: when splitting a shell window with C-x 3,
 * the non-active window gets wrap_cols set to the active window's cols
 * (which is smaller due to the separator), leaving a visible grey gutter.
 */

#include "testlib.h"

/* Replicate the key constants and arithmetic from the editor */
#define WF_POPUP      0x0001
#define WF_MODELINE   0x0002
#define WF_RSEPARATOR 0x0004
#define WF_MINIBUF    0x0020

static int max_int(int a, int b) { return a > b ? a : b; }
static int min_int(int a, int b) { return a < b ? a : b; }

/*
 * Minimal window geometry, mirroring the relevant fields of EditState.
 */
typedef struct {
    int x1, y1, x2, y2;   /* full window rectangle */
    int xleft, ytop;       /* client area origin */
    int width, height;     /* client area size */
    int cols, rows;        /* client area in character cells */
    int wrap_cols;         /* display width for WRAP_TERM mode */
    int flags;
} WindowGeom;

/*
 * Replicates compute_client_area() from qe.c:7210
 * separator_width and mode_line_height are 1 in TTY mode.
 */
static void compute_client_area(WindowGeom *w, int separator_width, int mode_line_height)
{
    int x1 = w->x1;
    int y1 = w->y1;
    int x2 = w->x2;
    int y2 = w->y2;

    if (w->flags & WF_MODELINE)
        y2 -= mode_line_height;
    if (w->flags & WF_RSEPARATOR)
        x2 -= separator_width;

    w->xleft = x1;
    w->ytop = y1;
    w->width = x2 - x1;
    w->height = y2 - y1;
    /* In TTY mode, char_width = line_height = 1 */
    w->cols = max_int(1, w->width);
    w->rows = max_int(1, w->height);
}

/*
 * Replicates the side-by-side branch of qe_split_window() from qe.c:9782
 * Returns the new (right) window; modifies the original (left) window in place.
 */
static void split_side_by_side(WindowGeom *left, WindowGeom *right, int prop,
                               int separator_width, int mode_line_height)
{
    int w = left->x2 - left->x1;
    int h = left->y2 - left->y1;
    int w1 = (w * min_int(prop, 100) + 50) / 100;

    /* New right window */
    right->x1 = left->x1 + w1;
    right->y1 = left->y1;
    right->x2 = left->x1 + w;  /* original right edge */
    right->y2 = left->y1 + h;
    right->flags = WF_MODELINE | (left->flags & WF_RSEPARATOR);

    /* Shrink left window and add separator */
    left->x2 = left->x1 + w1;
    left->flags |= WF_RSEPARATOR;

    compute_client_area(left, separator_width, mode_line_height);
    compute_client_area(right, separator_width, mode_line_height);
}

/*
 * Simulates the BUGGY wrap_cols propagation from shell_display_hook.
 * The active window drives s_cols, then ALL windows get s_cols.
 */
static void propagate_wrap_cols_buggy(WindowGeom *active, WindowGeom **windows, int n)
{
    int s_cols = active->cols;
    active->wrap_cols = s_cols;
    for (int i = 0; i < n; i++) {
        windows[i]->wrap_cols = s_cols;
    }
}

/*
 * Simulates the FIXED wrap_cols propagation.
 * The active window drives s_cols for PTY, but each window gets its own cols.
 */
static void propagate_wrap_cols_fixed(WindowGeom *active, WindowGeom **windows, int n)
{
    active->wrap_cols = active->cols;
    for (int i = 0; i < n; i++) {
        windows[i]->wrap_cols = windows[i]->cols;
    }
}

/* ---- Split geometry tests ---- */

TEST(split_window, two_way_no_gap) {
    /* Total window columns must equal left + right (no pixel gap) */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* The two windows must tile perfectly: left.x2 == right.x1 */
    ASSERT_EQ(left.x2, right.x1);
    /* Combined outer width equals original */
    ASSERT_EQ(right.x2 - left.x1, 204);
}

TEST(split_window, three_way_no_gap) {
    /* Split left again to get 3 panes */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom mid = {0}, right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);
    split_side_by_side(&left, &mid, 50, 1, 1);

    ASSERT_EQ(left.x2, mid.x1);
    ASSERT_EQ(mid.x2, right.x1);
    ASSERT_EQ(right.x2 - left.x1, 204);
}

TEST(split_window, separator_reduces_client_width) {
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* Left window has WF_RSEPARATOR, so client width is 1 less than outer */
    ASSERT_TRUE(left.flags & WF_RSEPARATOR);
    ASSERT_EQ(left.width, (left.x2 - left.x1) - 1);

    /* Right window (no separator) has client width == outer width */
    ASSERT_FALSE(right.flags & WF_RSEPARATOR);
    ASSERT_EQ(right.width, right.x2 - right.x1);
}

TEST(split_window, left_cols_smaller_than_right) {
    /* After a 50/50 split, the left window (with separator) has fewer cols */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* Left has separator stealing 1 column */
    ASSERT_TRUE(left.cols < right.cols);
}

/* ---- wrap_cols grey gutter tests ---- */

/*
 * The grey gutter appears when wrap_cols < window cols.
 * In display_init (WRAP_TERM), ds->width = wrap_cols.
 * In flush_line, if ds->width < e->width, the gap is filled with GUTTER style.
 */

TEST(split_window, buggy_wrap_cols_causes_gutter) {
    /* Reproduce the bug: after split, right window gets left's wrap_cols */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* Simulate buggy propagation (active = left) */
    WindowGeom *windows[] = { &left, &right };
    propagate_wrap_cols_buggy(&left, windows, 2);

    /* BUG: right window's wrap_cols == left's cols, which is smaller */
    ASSERT_EQ(right.wrap_cols, left.cols);
    /* This creates a gutter of (right.cols - right.wrap_cols) columns */
    int gutter = right.cols - right.wrap_cols;
    ASSERT_TRUE(gutter > 0);  /* grey bar visible! */
}

TEST(split_window, fixed_wrap_cols_no_gutter) {
    /* After fix: each window gets its own cols as wrap_cols */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* Simulate fixed propagation */
    WindowGeom *windows[] = { &left, &right };
    propagate_wrap_cols_fixed(&left, windows, 2);

    /* Each window's wrap_cols matches its own cols */
    ASSERT_EQ(left.wrap_cols, left.cols);
    ASSERT_EQ(right.wrap_cols, right.cols);

    /* No gutter on either window */
    ASSERT_EQ(right.cols - right.wrap_cols, 0);
    ASSERT_EQ(left.cols - left.wrap_cols, 0);
}

TEST(split_window, three_way_buggy_wrap_cols) {
    /* With 3-way split, the rightmost pane gets a large gutter.
     * Note: mid inherits WF_RSEPARATOR from the original left window,
     * so mid.cols == left.cols (both have separators). Only the rightmost
     * window (no separator) has a visible gutter.
     */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom mid = {0}, right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);
    split_side_by_side(&left, &mid, 50, 1, 1);

    /* Buggy propagation from leftmost (active) window */
    WindowGeom *windows[] = { &left, &mid, &right };
    propagate_wrap_cols_buggy(&left, windows, 3);

    /* Right window gets left's cols, creating a large gutter */
    int right_gutter = right.cols - right.wrap_cols;
    ASSERT_TRUE(right_gutter > 0);

    /* The gutter is substantial: right has no separator, left has one */
    ASSERT_TRUE(right_gutter > 1);
}

TEST(split_window, three_way_fixed_wrap_cols) {
    /* After fix: no gutter on any window */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 204, .y2 = 50,
                        .flags = WF_MODELINE };
    WindowGeom mid = {0}, right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);
    split_side_by_side(&left, &mid, 50, 1, 1);

    WindowGeom *windows[] = { &left, &mid, &right };
    propagate_wrap_cols_fixed(&left, windows, 3);

    ASSERT_EQ(left.wrap_cols, left.cols);
    ASSERT_EQ(mid.wrap_cols, mid.cols);
    ASSERT_EQ(right.wrap_cols, right.cols);

    /* No gutter on any window */
    ASSERT_EQ(left.cols - left.wrap_cols, 0);
    ASSERT_EQ(mid.cols - mid.wrap_cols, 0);
    ASSERT_EQ(right.cols - right.wrap_cols, 0);
}

TEST(split_window, odd_width_no_gap) {
    /* Odd total width: verify no columns are lost */
    WindowGeom left = { .x1 = 0, .y1 = 0, .x2 = 81, .y2 = 24,
                        .flags = WF_MODELINE };
    WindowGeom right = {0};

    split_side_by_side(&left, &right, 50, 1, 1);

    /* Windows tile perfectly */
    ASSERT_EQ(left.x2, right.x1);
    ASSERT_EQ(right.x2, 81);

    /* With fixed propagation, no gutter */
    WindowGeom *windows[] = { &left, &right };
    propagate_wrap_cols_fixed(&left, windows, 2);
    ASSERT_EQ(left.wrap_cols, left.cols);
    ASSERT_EQ(right.wrap_cols, right.cols);
}

int main(void) {
    return testlib_run_all();
}
