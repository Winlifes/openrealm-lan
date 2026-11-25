#ifdef BZ_TESTS
#include "test.h"
#include "../hud/hud_local.h"

/* Classic archives have no HD-only nodes. Keep that absence distinct from
 * the modern hierarchy, whose extra art must not cover the SD portrait. */
static handle_t (*cinematic_read_file)(cstring_t, uint32_t *);
static handle_t cinematic_classic_read(cstring_t name, uint32_t *size) {
    static char const classic[] =
        "Frame \"FRAME\" \"CinematicPanel\" { SetAllPoints,"
        " Frame \"BACKDROP\" \"CinematicBottomBorder\" { Width 0.8, Height 0.14, }"
        " Frame \"FRAME\" \"CinematicScenePanel\" { SetAllPoints,"
        "  Frame \"BACKDROP\" \"CinematicPortraitBackground\" { }"
        "  Frame \"SPRITE\" \"CinematicPortrait\" { Width 0.116, Height 0.116, }"
        "  Frame \"BACKDROP\" \"CinematicPortraitCover\" { }"
        "  Frame \"TEXT\" \"CinematicSpeakerText\" { }"
        "  Frame \"TEXT\" \"CinematicDialogueText\" { }"
        " } Frame \"BACKDROP\" \"CinematicTopBorder\" { } }";
    if (strcasecmp(name, "UI\\FrameDef\\UI\\CinematicPanel.fdf")) return cinematic_read_file(name, size);
    *size = sizeof(classic) - 1;
    string_t text = gi.MemAlloc(sizeof(classic));
    memcpy(text, classic, sizeof(classic));
    return text;
}

TEST(wc3_cinematic, sd_hud_excludes_modern_hd_art_and_keeps_authored_geometry) {
    frameDef_t *background, *cover;
    UI_ResetHud();
    UI_LoadHudCinematic();
    T_NOT_NULL(hud.cinematic.CinematicPanel);
    background = UI_FindChildFrame(hud.cinematic.CinematicPanel, "HDCinematicBackground");
    cover = UI_FindChildFrame(hud.cinematic.CinematicPanel, "HDCinematicPortraitCover");
    T_NOT_NULL(background); T_NOT_NULL(cover);
    if (background && cover) {
        T_ASSERT(background->hidden);
        T_ASSERT(cover->hidden);
    }
    T_ASSERT(!hud.cinematic.CinematicPortraitCover->hidden);
    T_ASSERT(!hud.cinematic.CinematicPortrait->hidden);
    T_FEQ(hud.cinematic.CinematicPortrait->Width, 0.116f, 0.0001f);
    T_FEQ(hud.cinematic.CinematicPortrait->Height, 0.116f, 0.0001f);
    T_FEQ(hud.cinematic.CinematicBottomBorder->Height, 0.14f, 0.0001f);
    UI_LoadHudCinematic();
    T_ASSERT(background && background->hidden);
    T_ASSERT(cover && cover->hidden);
    UI_ResetHud();
}

TEST(wc3_cinematic, classic_hud_without_hd_nodes_still_loads) {
    cinematic_read_file = gi.ReadBaseFile;
    gi.ReadBaseFile = cinematic_classic_read;
    UI_ResetHud();
    UI_LoadHudCinematic();
    T_NOT_NULL(hud.cinematic.CinematicPanel);
    T_NULL(UI_FindChildFrame(hud.cinematic.CinematicPanel, "HDCinematicBackground"));
    T_NULL(UI_FindChildFrame(hud.cinematic.CinematicPanel, "HDCinematicPortraitCover"));
    T_ASSERT(!hud.cinematic.CinematicPortrait->hidden);
    T_ASSERT(!hud.cinematic.CinematicBottomBorder->hidden);
    gi.ReadBaseFile = cinematic_read_file;
    UI_ResetHud();
}
#endif
