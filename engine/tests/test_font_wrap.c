/* Exercise the production text layout with synthetic fixed-width glyphs. No
 * retail font or GL context is needed to check Unicode line breaking. */
#include "test.h"
#include "renderer/r_font.c"

refImport_t ri;
struct render_globals tr;
rect_t R_UISceneRect(void) { return (rect_t){0}; }
texture_t *R_AllocateTexture(uint32_t w, uint32_t h) { (void)w; (void)h; return NULL; }
void R_ReleaseTexture(texture_t *t) { (void)t; }
void R_LoadTextureMipLevel(texture_t *t, texMip_t const *m) { (void)t; (void)m; }
void R_DrawImageEx(drawImage_t const *p) { (void)p; }
vertex_t *R_AddQuad(vertex_t *p, rect_t const *s, rect_t const *uv, color32_t c, float z) {
    (void)s; (void)uv; (void)c; (void)z; return p + 6;
}
void R_DrawImageBatch(texture_t const *t, SHADERTYPE shader, BLEND_MODE blend, float glow, float radial,
                      bool clipped, rect_t const *clip, vertex_t const *v, uint32_t count, bool repeat) {
    (void)t; (void)shader; (void)blend; (void)glow; (void)radial;
    (void)clipped; (void)clip; (void)v; (void)count; (void)repeat;
}

static void check_two_lines(cstring_t text, float cells) {
    glyphSet_t *glyphs = calloc(1, sizeof(*glyphs) + LATIN_GLYPHS * sizeof(glyphs->glyphs[0]));
    font_t font = { .height = 20, .size = 24 };
    FOR_LOOP(i, LATIN_GLYPHS) glyphs->glyphs[i].xadvance = 16;
    FOR_LOOP(i, GLYPH_PAGE_COUNT) font.sets[i] = glyphs;
    drawText_t args = { .text = text, .font = &font, .flags = DRAW_WORD_WRAP,
                       .textWidth = cells * INV_SCALE_X(16) };
    vec2_t size = R_GetTextSize(&args);
    free(glyphs);
    T_FEQ(size.y, 2 * R_GetFontHeight(&font), .00001f);
    T_ASSERT(R_TextFitsWidth(args.textWidth - size.x));
}

TEST(font_wrap, adjacent_unicode_pages_keep_distinct_widths) {
    font_t font = { .height = 20, .size = 24 };
    glyphSet_t *left = calloc(1, sizeof(*left) + GLYPH_PAGE_SIZE * sizeof(left->glyphs[0]));
    glyphSet_t *right = calloc(1, sizeof(*right) + GLYPH_PAGE_SIZE * sizeof(right->glyphs[0]));
    unsigned const first = 0x4e00u;
    unsigned const page = 1u + (first - LATIN_GLYPHS) / GLYPH_PAGE_SIZE;
    left->glyphs[GLYPH_PAGE_SIZE - 1].xadvance = 16;
    right->glyphs[0].xadvance = 24;
    font.sets[page] = left;
    font.sets[page + 1] = right;
    float const width = R_GetFontWidth(&font, "丟丠");
    free(right);
    free(left);
    T_FEQ(width, INV_SCALE_X(40), .00001f);
}

TEST(font_wrap, chinese_breaks_between_glyphs_instead_of_before_every_glyph) {
    check_two_lines("农夫可以采矿", 3);
}
TEST(font_wrap, chinese_color_runs_keep_normal_line_width) {
    check_two_lines("农夫|cffffcc00可以|r采矿", 3);
}
TEST(font_wrap, latin_words_still_wrap_at_spaces) {
    check_two_lines("abc def", 4);
}
