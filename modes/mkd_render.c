/*
 * Markdown render mode for QEmacs.
 *
 * Provides a read-only rendered view of markdown files with:
 *   - Unicode bullets for lists
 *   - Hidden markup characters (concealed #, *, _, `)
 *   - Styled headings, bold, italic, code, links
 *   - Blockquote bars and horizontal rules
 *
 * Toggle with: M-x mkd-render-mode  or  C-c C-r
 *
 * Copyright (c) 2026 QEmacs contributors.
 * MIT License.
 */

#include "qe.h"
#include "mkd_render.h"

/* mkd_render_parse.c is compiled as a separate translation unit and
 * linked in. Its public API is declared in mkd_render.h. */

/*----------------------------------------------------------------------
 * Render styles — mapped to existing QE style constants.
 *----------------------------------------------------------------------*/

enum {
    MKD_RENDER_HEADING1    = QE_STYLE_FUNCTION,    /* blue */
    MKD_RENDER_HEADING2    = QE_STYLE_KEYWORD,     /* cyan */
    MKD_RENDER_HEADING3    = QE_STYLE_VARIABLE,    /* yellow */
    MKD_RENDER_HEADING4    = QE_STYLE_TYPE,         /* green */
    MKD_RENDER_HEADING5    = QE_STYLE_STRING,       /* orange */
    MKD_RENDER_HEADING6    = QE_STYLE_COMMENT,      /* red-orange */
    MKD_RENDER_CODE        = QE_STYLE_STRING,
    MKD_RENDER_BLOCKQUOTE  = QE_STYLE_COMMENT,
    MKD_RENDER_LINK        = QE_STYLE_KEYWORD,
    MKD_RENDER_HR          = QE_STYLE_GUTTER,
    MKD_RENDER_BOLD        = QE_STYLE_KEYWORD,
    MKD_RENDER_ITALIC      = QE_STYLE_VARIABLE,
    MKD_RENDER_LIST_MARKER = QE_STYLE_FUNCTION,
    MKD_RENDER_STRIKE      = QE_STYLE_COMMENT,
    MKD_RENDER_TABLE       = QE_STYLE_TYPE,
};

static const int heading_styles[] = {
    MKD_RENDER_HEADING1, MKD_RENDER_HEADING2, MKD_RENDER_HEADING3,
    MKD_RENDER_HEADING4, MKD_RENDER_HEADING5, MKD_RENDER_HEADING6,
};

/* Unicode decorators for headings (prepended) */
static const char32_t heading_decorators[] = {
    0x2588,  /* █ FULL BLOCK for H1 */
    0x2589,  /* ▉ for H2 */
    0x258A,  /* ▊ for H3 */
    0x258B,  /* ▋ for H4 */
    0x258C,  /* ▌ for H5 */
    0x258D,  /* ▍ for H6 */
};

/* Blockquote bar character */
#define BQ_BAR  0x2502  /* │ BOX DRAWINGS LIGHT VERTICAL */

/* Horizontal rule character */
#define HR_CHAR 0x2501  /* ━ BOX DRAWINGS HEAVY HORIZONTAL */

/*----------------------------------------------------------------------
 * Per-window render state: tracks code fence across lines
 *----------------------------------------------------------------------*/

typedef struct MkdRenderState {
    QEModeData base;
    int in_code_block;  /* nonzero if inside a fenced code block */
} MkdRenderState;

static ModeDef mkd_render_mode;

/*----------------------------------------------------------------------
 * Render display_line: reads buffer, outputs styled characters
 *----------------------------------------------------------------------*/

/*
 * Emit inline-formatted text, concealing markdown markers.
 * Characters from buf[content_start..len) are displayed, with spans.
 */
static void mkd_render_emit_inline(DisplayState *ds,
                                   const char32_t *buf, int len,
                                   int content_start, int line_offset,
                                   QETermStyle base_style)
{
    MkdRenderSpan spans[32];
    int nspans, i, sp, k;

    nspans = mkd_render_find_spans(buf + content_start, spans, 32);

    /* Adjust span offsets relative to full line */
    for (k = 0; k < nspans; k++) {
        spans[k].start += content_start;
        spans[k].end += content_start;
        spans[k].content_start += content_start;
        spans[k].content_end += content_start;
    }

    i = content_start;
    sp = 0;

    while (i < len) {
        if (sp < nspans && i == spans[sp].start) {
            /* At a span — skip opening markers, display content, skip closing */
            QETermStyle span_style;
            int j;

            switch (spans[sp].type) {
            case MKD_SPAN_BOLD:         span_style = MKD_RENDER_BOLD; break;
            case MKD_SPAN_ITALIC:       span_style = MKD_RENDER_ITALIC; break;
            case MKD_SPAN_CODE:         span_style = MKD_RENDER_CODE; break;
            case MKD_SPAN_LINK:         span_style = MKD_RENDER_LINK; break;
            case MKD_SPAN_STRIKETHROUGH: span_style = MKD_RENDER_STRIKE; break;
            default:                    span_style = base_style; break;
            }

            /* Display content only (skip markers) */
            ds->style = span_style;
            for (j = spans[sp].content_start; j < spans[sp].content_end; j++) {
                display_char(ds, line_offset + j, line_offset + j + 1, buf[j]);
            }

            i = spans[sp].end;
            sp++;
        } else {
            /* Normal character */
            ds->style = base_style;
            display_char(ds, line_offset + i, line_offset + i + 1, buf[i]);
            i++;
        }
    }
}

static int mkd_render_display_line(EditState *s, DisplayState *ds, int offset)
{
    char32_t buf[MAX_SCREEN_WIDTH];
    int len, next_offset;
    int line_type;
    MkdRenderState *rs;

    rs = qe_get_window_mode_data(s, &mkd_render_mode, 0);

    /* Read the line from buffer */
    len = eb_get_line(s->b, buf, MAX_SCREEN_WIDTH - 1, offset, &next_offset);
    if (len < 0)
        len = 0;
    buf[len] = 0;

    display_bol(ds);

    /* Handle code block continuation */
    if (rs && rs->in_code_block) {
        line_type = mkd_render_classify_line(buf);
        if (line_type == MKD_LINE_CODE_FENCE) {
            /* Closing fence — render separator and end block */
            int i;
            rs->in_code_block = 0;
            ds->style = MKD_RENDER_CODE;
            for (i = 0; i < 40; i++)
                display_char(ds, -1, -1, 0x2500);  /* ─ */
            display_eol(ds, offset, next_offset);
            return next_offset;
        }
        /* Inside code block: display with code style, with indent */
        {
            int i;
            ds->style = MKD_RENDER_CODE;
            display_char(ds, -1, -1, ' ');
            display_char(ds, -1, -1, ' ');
            for (i = 0; i < len; i++) {
                display_char(ds, offset + i, offset + i + 1, buf[i]);
            }
            display_eol(ds, offset + len, next_offset);
        }
        return next_offset;
    }

    line_type = mkd_render_classify_line(buf);

    switch (line_type) {
    case MKD_LINE_HEADING: {
        int content_off;
        int level = mkd_render_heading_level(buf, &content_off);
        QETermStyle style = (QETermStyle)heading_styles[(level - 1) % 6];

        /* Heading decorator + space */
        ds->style = style;
        display_char(ds, -1, -1, heading_decorators[(level - 1) % 6]);
        display_char(ds, -1, -1, ' ');

        /* Render heading text with inline formatting */
        mkd_render_emit_inline(ds, buf, len, content_off, offset, style);
        display_eol(ds, offset + len, next_offset);
        break;
    }

    case MKD_LINE_CODE_FENCE: {
        int i;
        /* Opening fence — enter code block */
        if (rs)
            rs->in_code_block = 1;
        ds->style = MKD_RENDER_CODE;
        /* Show a thin separator line */
        for (i = 0; i < 40; i++)
            display_char(ds, -1, -1, 0x2500);  /* ─ */
        display_eol(ds, offset, next_offset);
        break;
    }

    case MKD_LINE_BLOCKQUOTE: {
        int content_off, depth, d;
        depth = mkd_render_blockquote_depth(buf, &content_off);
        /* Draw vertical bars for blockquote depth */
        for (d = 0; d < depth; d++) {
            ds->style = MKD_RENDER_BLOCKQUOTE;
            display_char(ds, -1, -1, BQ_BAR);
            display_char(ds, -1, -1, ' ');
        }
        /* Render content with inline formatting */
        mkd_render_emit_inline(ds, buf, len, content_off,
                               offset, MKD_RENDER_BLOCKQUOTE);
        display_eol(ds, offset + len, next_offset);
        break;
    }

    case MKD_LINE_LIST_ITEM: {
        int depth, ordered, content_off, d;
        mkd_render_parse_list_item(buf, &depth, &ordered, &content_off);

        /* Indent for depth */
        for (d = 0; d < depth; d++) {
            display_char(ds, -1, -1, ' ');
            display_char(ds, -1, -1, ' ');
        }

        /* Bullet or number */
        ds->style = MKD_RENDER_LIST_MARKER;
        if (ordered) {
            /* Display the number from the source */
            int i = 0;
            while (buf[i] == ' ' || buf[i] == '\t') i++;
            while (buf[i] >= '0' && buf[i] <= '9') {
                display_char(ds, offset + i, offset + i + 1, buf[i]);
                i++;
            }
            display_char(ds, -1, -1, '.');
        } else {
            char32_t bullet = mkd_render_bullet_char(depth, 0);
            display_char(ds, -1, -1, bullet);
        }
        display_char(ds, -1, -1, ' ');

        /* Render list content with inline formatting */
        mkd_render_emit_inline(ds, buf, len, content_off, offset, 0);
        display_eol(ds, offset + len, next_offset);
        break;
    }

    case MKD_LINE_HR: {
        int i;
        /* Full-width horizontal rule */
        ds->style = MKD_RENDER_HR;
        for (i = 0; i < ds->width; i++)
            display_char(ds, -1, -1, HR_CHAR);
        display_eol(ds, offset, next_offset);
        break;
    }

    case MKD_LINE_TABLE: {
        int i;
        /* Render table row with box-drawing chars */
        ds->style = MKD_RENDER_TABLE;
        for (i = 0; i < len; i++) {
            char32_t c = buf[i];
            if (c == '|')
                c = 0x2502;  /* │ */
            else if (c == '-')
                c = 0x2500;  /* ─ */
            display_char(ds, offset + i, offset + i + 1, c);
        }
        display_eol(ds, offset + len, next_offset);
        break;
    }

    case MKD_LINE_INDENTED_CODE: {
        /* Indented code block (4+ spaces): strip the indent prefix,
         * display content with code style and a 2-space visual indent */
        int i, content_start = 0;
        int ind = 0;
        /* skip the leading 4 spaces / 1 tab of code indent */
        while (ind < 4 && buf[content_start]) {
            if (buf[content_start] == '\t') {
                ind += 4;
            } else if (buf[content_start] == ' ') {
                ind++;
            } else {
                break;
            }
            content_start++;
        }
        ds->style = MKD_RENDER_CODE;
        display_char(ds, -1, -1, ' ');
        display_char(ds, -1, -1, ' ');
        for (i = content_start; i < len; i++) {
            display_char(ds, offset + i, offset + i + 1, buf[i]);
        }
        display_eol(ds, offset + len, next_offset);
        break;
    }

    case MKD_LINE_BLANK:
        display_eol(ds, offset, next_offset);
        break;

    case MKD_LINE_PARAGRAPH:
    default:
        /* Normal paragraph with inline formatting */
        mkd_render_emit_inline(ds, buf, len, 0, offset, 0);
        display_eol(ds, offset + len, next_offset);
        break;
    }

    return next_offset;
}

/*----------------------------------------------------------------------
 * Mode definition and commands
 *----------------------------------------------------------------------*/

static int mkd_render_mode_init(EditState *s, EditBuffer *b, int flags)
{
    MkdRenderState *rs;

    if (s) {
        s->wrap = WRAP_WORD;
        s->b->tab_width = 4;
        s->indent_tabs_mode = 0;

        rs = qe_get_window_mode_data(s, &mkd_render_mode, 1);
        if (rs) {
            rs->in_code_block = 0;
        }
    }
    return 0;
}

static void do_mkd_render_mode(EditState *s)
{
    /* Toggle between markdown edit mode and render mode */
    if (s->mode == &mkd_render_mode) {
        /* Switch back to markdown mode */
        ModeDef *m = qe_find_mode(s->qs, "markdown", MODEF_SYNTAX);
        if (m)
            edit_set_mode(s, m);
    } else {
        edit_set_mode(s, &mkd_render_mode);
    }
}

static const CmdDef mkd_render_commands[] = {
    CMD0( "mkd-render-mode", "C-c C-r",
          "Toggle markdown render/edit mode",
          do_mkd_render_mode)
};

static ModeDef mkd_render_mode = {
    .name = "mkd-render",
    .desc = "Rendered markdown view",
    .mode_init = mkd_render_mode_init,
    .display_line = mkd_render_display_line,
    .window_instance_size = sizeof(MkdRenderState),
    .default_wrap = WRAP_WORD,
};

static int mkd_render_init(QEmacsState *qs)
{
    ModeDef *mkd_mode;

    qe_register_mode(qs, &mkd_render_mode, MODEF_VIEW);
    qe_register_commands(qs, NULL, mkd_render_commands,
                         countof(mkd_render_commands));

    /* Also register C-c C-r in markdown mode */
    mkd_mode = qe_find_mode(qs, "markdown", MODEF_SYNTAX);
    if (mkd_mode) {
        qe_register_commands(qs, mkd_mode, mkd_render_commands,
                             countof(mkd_render_commands));
    }

    return 0;
}

qe_module_init(mkd_render_init);
