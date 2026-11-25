/* Browser DOM loading presentation adapter. Never blocks the engine worker. */
#ifdef __EMSCRIPTEN__
#include "common/common.h"
#ifdef BZ_WEB_ASYNC_ASSETS
#include <emscripten.h>
#endif
#include <emscripten/em_asm.h>
void Sys_UpdateLoadingPresentation(bool active, float progress, bool loading_screen_submitted) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' &&
            typeof window.openrealmEarlyMapLoading === 'function')
            window.openrealmEarlyMapLoading($0 !== 0, $1, $2 !== 0);
    }, active ? 1 : 0, progress, loading_screen_submitted ? 1 : 0);
#ifdef BZ_WEB_ASYNC_ASSETS
    if (active) emscripten_sleep(16);
#endif
}

void Sys_UpdateMatchLoadingPlayers(cstring_t progress) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined') window.openrealmMatchLoadingPlayers?.(UTF8ToString($0));
    }, progress);
}

void Sys_UpdateRoomPlayerName(uint32_t player, cstring_t name) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined') window.openrealmRoomPlayerName?.($0, UTF8ToString($1));
    }, player, name);
}
#endif
