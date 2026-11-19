/* Compile the production browser transport against a deterministic WebSocket
 * API fixture. These callback tests do not require a browser or game assets. */
#include "../common/common.h"
#include "test.h"

typedef bool EM_BOOL;
#define EM_TRUE true
typedef int EMSCRIPTEN_WEBSOCKET_T;
typedef struct { int socket; } EmscriptenWebSocketOpenEvent;
typedef struct { int socket; } EmscriptenWebSocketErrorEvent;
typedef struct { int socket; bool wasClean; unsigned short code; char reason[512]; } EmscriptenWebSocketCloseEvent;
typedef struct { int socket; uint8_t *data; uint32_t numBytes; bool isText; } EmscriptenWebSocketMessageEvent;
typedef struct { const char *url, *protocols; bool createOnMainThread; } EmscriptenWebSocketCreateAttributes;
static int next_socket, created, deleted, closed, sent;
static EM_BOOL (*onopen)(int, EmscriptenWebSocketOpenEvent const *, void *);
static EM_BOOL (*onerror)(int, EmscriptenWebSocketErrorEvent const *, void *);
static EM_BOOL (*onclose)(int, EmscriptenWebSocketCloseEvent const *, void *);
static EM_BOOL (*onmessage)(int, EmscriptenWebSocketMessageEvent const *, void *);
static int emscripten_websocket_new(EmscriptenWebSocketCreateAttributes *attr) {
    T_STREQ(attr->url, "wss://transport.test/room");
    created++;
    return ++next_socket;
}
static void emscripten_websocket_init_create_attributes(EmscriptenWebSocketCreateAttributes *attr) { memset(attr, 0, sizeof(*attr)); }
#define emscripten_websocket_set_onopen_callback(s,u,f) (onopen=(f))
#define emscripten_websocket_set_onerror_callback(s,u,f) (onerror=(f))
#define emscripten_websocket_set_onclose_callback(s,u,f) (onclose=(f))
#define emscripten_websocket_set_onmessage_callback(s,u,f) (onmessage=(f))
static int emscripten_websocket_send_binary(int socket, void *data, uint32_t size) {
    T_EQ(socket, next_socket); T_EQ(size, 3); T_ASSERT(!memcmp(data, "abc", 3)); sent++; return 0;
}
static int emscripten_websocket_delete(int socket) { T_EQ(socket, next_socket); deleted++; return 0; }
static int emscripten_websocket_close(int socket, unsigned short code, const char *reason) {
    T_EQ(socket, next_socket); T_EQ(code, 1000); T_STREQ(reason, "Client disconnected"); closed++; return 0;
}

#include "../common/net_websocket.h"

TEST(browser_net, server_close_releases_transport_and_session_packets) {
    netadr_t peer = { .type = NA_IP }, from;
    uint8_t bytes[8];
    sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
    created = deleted = sent = 0;
    NET_WS_Open("wss://transport.test/room");
    NET_WS_Open("wss://transport.test/room");
    T_EQ(created, 1); /* repeated configure must retain the active bridge */
    NET_WS_Send(3, "abc", &peer);
    T_EQ(sent, 0);
    onopen(0, &(EmscriptenWebSocketOpenEvent){ .socket = next_socket }, NULL);
    T_EQ(sent, 1);
    onmessage(0, &(EmscriptenWebSocketMessageEvent){ .socket = next_socket, .data = (uint8_t *)"abc", .numBytes = 3 }, NULL);
    T_EQ(net_ws_received, 1); T_EQ(net_ws_delivered, 0); T_EQ(net_ws_dropped, 0);
    onclose(0, &(EmscriptenWebSocketCloseEvent){ .socket = next_socket }, NULL);
    T_ASSERT(!NET_WS_Active());
    T_EQ(NET_WS_GetPacket(&from, &msg), 0); /* no previous-room snapshot */
    T_EQ(net_ws_received, 0); T_EQ(net_ws_delivered, 0); T_EQ(net_ws_dropped, 0);
    T_EQ(deleted, 1);
    NET_WS_Open("wss://transport.test/room");
    T_EQ(created, 2);
    onerror(0, &(EmscriptenWebSocketErrorEvent){ .socket = next_socket }, NULL);
    onclose(0, &(EmscriptenWebSocketCloseEvent){ .socket = next_socket }, NULL);
}

TEST(browser_net, local_close_drains_socket_once_and_discards_connecting_input) {
    netadr_t peer = { .type = NA_IP }, from;
    uint8_t bytes[8];
    sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
    created = deleted = closed = sent = 0;
    NET_WS_Open("wss://transport.test/room");
    int previous = next_socket;
    NET_WS_Send(3, "abc", &peer); /* handshake has not opened yet */
    NET_WS_Release(true);
    NET_WS_Release(true);
    T_ASSERT(!NET_WS_Active()); T_EQ(closed, 1); T_EQ(deleted, 1);
    NET_WS_Open("wss://transport.test/room");
    /* Late events from the released handle cannot open, inject data into, or
     * close the new room. The old unsent input must not be flushed either. */
    onopen(0, &(EmscriptenWebSocketOpenEvent){ .socket = previous }, NULL);
    onmessage(0, &(EmscriptenWebSocketMessageEvent){ .socket = previous, .data = (uint8_t *)"abc", .numBytes = 3 }, NULL);
    onclose(0, &(EmscriptenWebSocketCloseEvent){ .socket = previous }, NULL);
    T_ASSERT(NET_WS_Active()); T_EQ(sent, 0); T_EQ(deleted, 1);
    onopen(0, &(EmscriptenWebSocketOpenEvent){ .socket = next_socket }, NULL);
    T_EQ(sent, 0);
    NET_WS_Send(3, "abc", &peer);
    onmessage(0, &(EmscriptenWebSocketMessageEvent){ .socket = next_socket, .data = (uint8_t *)"abc", .numBytes = 3 }, NULL);
    T_EQ(NET_WS_GetPacket(&from, &msg), 3);
    T_EQ(net_ws_received, 1); T_EQ(net_ws_delivered, 1); T_EQ(net_ws_dropped, 0);
    T_ASSERT(!memcmp(bytes, "abc", 3));
    T_EQ(NET_WS_GetPacket(&from, &msg), 0);
    NET_WS_Release(true);
    T_EQ(sent, 1); T_EQ(closed, 2); T_EQ(deleted, 2);
}
