/* Browser transport implementation, also compiled by the native callback-contract tests.
 * The caller supplies the Emscripten WebSocket API and the shared network types. */
#pragma once

/* Browser client transport: the engine speaks UDP; browsers only speak
 * WebSocket.  NET_OpenIP(NS_CLIENT) opens a WebSocket to the ws bridge
 * (cvar ws_url) and every packet addressed to a remote NA_IP address is
 * sent as one binary WS frame; frames from the bridge are queued here and
 * surface through NET_GetPacket with the last destination as the peer. */
#define NET_WS_QUEUE_LEN 128
static EMSCRIPTEN_WEBSOCKET_T net_ws_socket = 0;
static bool net_ws_peer_valid = false;
static netadr_t net_ws_peer;
static struct {
    uint32_t len;
    uint8_t data[MAX_MSGLEN];
} net_ws_queue[NET_WS_QUEUE_LEN];
static uint32_t net_ws_head, net_ws_tail;
static uint32_t net_ws_received, net_ws_delivered, net_ws_dropped;

static bool NET_WS_Active(void) { return net_ws_socket > 0; }

/* Packets produced before the browser WebSocket finishes opening are queued
 * here and flushed by the open callback; the client sends its connect
 * handshake immediately and must not lose it to the async socket. */
#define NET_WS_PENDING_LEN 32
static bool net_ws_opened = false;
static struct {
    uint32_t len;
    uint8_t data[MAX_MSGLEN];
} net_ws_pending[NET_WS_PENDING_LEN];
static uint32_t net_ws_pending_count;

/* Closing a browser session is also a transport boundary. A deleted handle
 * must never retain queued input or snapshots for a later room connection. */
static void NET_WS_Release(bool close_socket) {
    EMSCRIPTEN_WEBSOCKET_T socket = net_ws_socket;
    net_ws_socket = 0;
    net_ws_opened = false;
    net_ws_peer_valid = false;
    memset(&net_ws_peer, 0, sizeof(net_ws_peer));
    net_ws_head = net_ws_tail = net_ws_pending_count = 0;
    net_ws_received = net_ws_delivered = net_ws_dropped = 0;
    if (socket <= 0) return;
    if (close_socket) emscripten_websocket_close(socket, 1000, "Client disconnected");
    emscripten_websocket_delete(socket);
}

static void NET_WS_FlushPending(void) {
    FOR_LOOP(i, net_ws_pending_count) {
        emscripten_websocket_send_binary(net_ws_socket, net_ws_pending[i].data, net_ws_pending[i].len);
    }
    net_ws_pending_count = 0;
}

static EM_BOOL NET_WS_OnOpen(int eventType, EmscriptenWebSocketOpenEvent const *event, void *userData) {
    (void)eventType; (void)userData;
    if (!event || event->socket != net_ws_socket) return EM_TRUE;
    net_ws_opened = true;
    fprintf(stderr, "NET: websocket bridge connected\n");
    NET_WS_FlushPending();
    return EM_TRUE;
}
static EM_BOOL NET_WS_OnError(int eventType, EmscriptenWebSocketErrorEvent const *event, void *userData) {
    (void)eventType; (void)event; (void)userData;
    fprintf(stderr, "NET: websocket bridge error\n");
    return EM_TRUE;
}
static EM_BOOL NET_WS_OnClose(int eventType, EmscriptenWebSocketCloseEvent const *event, void *userData) {
    (void)eventType; (void)userData;
    if (!event || event->socket != net_ws_socket) return EM_TRUE;
    fprintf(stderr, "NET: websocket bridge closed\n");
    NET_WS_Release(false);
    return EM_TRUE;
}
static EM_BOOL NET_WS_OnMessage(int eventType, EmscriptenWebSocketMessageEvent const *event, void *userData) {
    uint32_t next;
    (void)eventType; (void)userData;
    if (!event || event->socket != net_ws_socket || event->isText || !event->numBytes) return EM_TRUE;
    net_ws_received++;
    next = (net_ws_head + 1) % NET_WS_QUEUE_LEN;
    if (next == net_ws_tail) { net_ws_dropped++; return EM_TRUE; }
    net_ws_queue[net_ws_head].len = event->numBytes <= MAX_MSGLEN ? (uint32_t)event->numBytes : MAX_MSGLEN;
    memcpy(net_ws_queue[net_ws_head].data, event->data, net_ws_queue[net_ws_head].len);
    net_ws_head = next;
    return EM_TRUE;
}

static void NET_WS_Open(cstring_t url) {
    EmscriptenWebSocketCreateAttributes attr;

    /* One bridge socket per client: the engine re-runs NET_OpenIP whenever the
     * client transport is configured (e.g. each LAN refresh), and reopening
     * would orphan the live socket and send through a CONNECTING one (the
     * emscripten glue throws InvalidStateError on send, which surfaces as an
     * uncaught page error and kills presentation). */
    if (NET_WS_Active()) return;
    net_ws_opened = false;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = url;
    net_ws_socket = emscripten_websocket_new(&attr);
    if (net_ws_socket <= 0) {
        fprintf(stderr, "NET: websocket creation failed for %s\n", url);
        net_ws_socket = 0;
        return;
    }
    emscripten_websocket_set_onopen_callback(net_ws_socket, NULL, NET_WS_OnOpen);
    emscripten_websocket_set_onerror_callback(net_ws_socket, NULL, NET_WS_OnError);
    emscripten_websocket_set_onclose_callback(net_ws_socket, NULL, NET_WS_OnClose);
    emscripten_websocket_set_onmessage_callback(net_ws_socket, NULL, NET_WS_OnMessage);
    fprintf(stderr, "NET: websocket opening %s\n", url);
}

static void NET_WS_Send(int length, void const *data, netadr_t const *to) {
    if (!NET_WS_Active() || length <= 0) return;
    net_ws_peer = *to;
    net_ws_peer_valid = true;
    if (!net_ws_opened) {
        if (net_ws_pending_count < NET_WS_PENDING_LEN && length <= MAX_MSGLEN) {
            net_ws_pending[net_ws_pending_count].len = (uint32_t)length;
            memcpy(net_ws_pending[net_ws_pending_count].data, data, (size_t)length);
            net_ws_pending_count++;
        }
        return;
    }
    emscripten_websocket_send_binary(net_ws_socket, (void *)data, (size_t)length);
}

static int NET_WS_GetPacket(netadr_t *from, sizeBuf_t *msg) {
    uint32_t len;
    if (net_ws_tail == net_ws_head || !net_ws_peer_valid) return 0;
    len = net_ws_queue[net_ws_tail].len;
    if (len > (uint32_t)msg->maxsize) {
        fprintf(stderr, "NET: websocket packet too large (%u)\n", (unsigned)len);
        net_ws_tail = (net_ws_tail + 1) % NET_WS_QUEUE_LEN;
        return 0;
    }
    memcpy(msg->data, net_ws_queue[net_ws_tail].data, len);
    msg->cursize = len;
    msg->readcount = 0;
    *from = net_ws_peer;
    net_ws_tail = (net_ws_tail + 1) % NET_WS_QUEUE_LEN;
    net_ws_delivered++;
    return (int)len;
}
