#include "web_chat.h"
#include <emscripten/emscripten.h>

EM_JS(int, Web_SetChatInput, (int active), {
    const editor = Module['gameChatInput'];
    if (!editor) return 0;
    if (active) editor.open();
    else editor.close();
    return 1;
});

EM_JS_DEPS(web_chat, "$stringToUTF8");
EM_JS(int, Web_ReadChatInput, (char *text, int capacity), {
    const line = Module['gameChatInput']?.take();
    if (line === undefined) return 0;
    stringToUTF8(line, Number(text), capacity);
    return 1;
});
