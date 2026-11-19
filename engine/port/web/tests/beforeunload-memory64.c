#include <assert.h>
#include <stdbool.h>
#include <emscripten.h>
#include <emscripten/html5.h>

static bool unsaved = true;
static const char *before_unload(int event, const void *reserved, void *user) {
    assert(event == EMSCRIPTEN_EVENT_BEFOREUNLOAD);
    assert(reserved == NULL && user == (void *)123);
    return unsaved ? "Unsaved player progress" : NULL;
}

int main(void) {
    assert(emscripten_set_beforeunload_callback((void *)123, before_unload) == EMSCRIPTEN_RESULT_SUCCESS);
    EM_ASM({
        var event = ({ prevented: false, preventDefault() { this.prevented = true; } });
        globalThis.testBeforeUnload(event);
        if (event.returnValue !== 'Unsaved player progress' || !event.prevented)
            throw Error('beforeunload must preserve the native warning and cancel navigation');
    });
    unsaved = false;
    EM_ASM({
        var event = { preventDefault() { throw Error('clean session blocked navigation'); } };
        if (globalThis.testBeforeUnload(event) !== undefined || event.returnValue !== undefined)
            throw Error('null callback result must allow navigation');
    });
    assert(emscripten_set_beforeunload_callback(NULL, NULL) == EMSCRIPTEN_RESULT_SUCCESS);
    EM_ASM({ if (globalThis.testBeforeUnload) throw Error('beforeunload callback was not removed'); });
    return 0;
}
