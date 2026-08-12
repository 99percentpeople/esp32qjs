#include "esp32_mquickjs_socket.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET

#include "esp32_mquickjs_core.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define SOCKET_HOST_MAX_BYTES 253U
#define SOCKET_WAIT_SLICE_MS 10U
#define SOCKET_DEFAULT_CONNECT_TIMEOUT_MS 5000
#define SOCKET_MAX_TIMEOUT_MS 60000
#define SOCKET_DEFAULT_LISTEN_BACKLOG 4

typedef enum {
    SOCKET_PROTOCOL_NONE = 0,
    SOCKET_PROTOCOL_TCP,
    SOCKET_PROTOCOL_UDP,
} socket_protocol_t;

typedef struct {
    int id;
    int fd;
    socket_protocol_t protocol;
    bool connected;
    bool listening;
    bool peer_closed;
    int local_port;
    int remote_port;
    char local_ip[INET6_ADDRSTRLEN];
    char remote_ip[SOCKET_HOST_MAX_BYTES + 1U];
    uint32_t sent_bytes;
    uint32_t received_bytes;
} socket_entry_t;

typedef struct {
    bool initialized;
    int next_id;
    socket_entry_t entries[CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES];
} socket_state_t;

static socket_state_t s_socket;

static const char *socket_protocol_name(socket_protocol_t protocol)
{
    return protocol == SOCKET_PROTOCOL_TCP ? "tcp" :
           protocol == SOCKET_PROTOCOL_UDP ? "udp" : "unknown";
}

static void socket_reset_entry(socket_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
    entry->fd = -1;
}

static socket_entry_t *socket_find_entry(int id)
{
    size_t i;

    if (id <= 0) {
        return NULL;
    }
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES; ++i) {
        if (s_socket.entries[i].id == id) {
            return &s_socket.entries[i];
        }
    }
    return NULL;
}

static socket_entry_t *socket_allocate_entry(void)
{
    size_t i;

    for (i = 0; i < CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES; ++i) {
        socket_entry_t *entry = &s_socket.entries[i];

        if (entry->id != 0) {
            continue;
        }
        socket_reset_entry(entry);
        s_socket.next_id++;
        if (s_socket.next_id <= 0) {
            s_socket.next_id = 1;
        }
        entry->id = s_socket.next_id;
        return entry;
    }
    return NULL;
}

static void socket_close_entry(socket_entry_t *entry)
{
    if (entry == NULL || entry->id == 0) {
        return;
    }
    if (entry->fd >= 0) {
        shutdown(entry->fd, SHUT_RDWR);
        close(entry->fd);
    }
    socket_reset_entry(entry);
}

static bool socket_to_int(JSContext *ctx,
                          JSValue value,
                          int min_value,
                          int max_value,
                          int *out_value)
{
    int raw_value;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 ||
        raw_value < min_value || raw_value > max_value) {
        return false;
    }
    *out_value = raw_value;
    return true;
}

static bool socket_optional_int(JSContext *ctx,
                                int argc,
                                JSValue *argv,
                                int index,
                                int default_value,
                                int min_value,
                                int max_value,
                                int *out_value)
{
    if (index >= argc || JS_IsUndefined(argv[index])) {
        *out_value = default_value;
        return true;
    }
    return socket_to_int(ctx, argv[index], min_value, max_value, out_value);
}

static socket_entry_t *socket_require_entry(JSContext *ctx,
                                            JSValue value,
                                            socket_protocol_t protocol)
{
    socket_entry_t *entry;
    int id;

    if (!socket_to_int(ctx, value, 1, INT32_MAX, &id)) {
        JS_ThrowTypeError(ctx, "invalid socket_id");
        return NULL;
    }
    entry = socket_find_entry(id);
    if (entry == NULL) {
        JS_ThrowRangeError(ctx, "unknown socket_id");
        return NULL;
    }
    if (protocol != SOCKET_PROTOCOL_NONE && entry->protocol != protocol) {
        JS_ThrowTypeError(ctx, "socket protocol does not match operation");
        return NULL;
    }
    return entry;
}

static bool socket_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool socket_format_address(const struct sockaddr_storage *address,
                                  char *host,
                                  size_t host_size,
                                  int *port)
{
    const void *raw_address;

    if (address->ss_family == AF_INET) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)address;

        raw_address = &ipv4->sin_addr;
        *port = ntohs(ipv4->sin_port);
    } else if (address->ss_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)address;

        raw_address = &ipv6->sin6_addr;
        *port = ntohs(ipv6->sin6_port);
    } else {
        return false;
    }
    return inet_ntop(address->ss_family, raw_address, host, host_size) != NULL;
}

static void socket_refresh_local_address(socket_entry_t *entry)
{
    struct sockaddr_storage address;
    socklen_t address_len = sizeof(address);

    if (getsockname(entry->fd, (struct sockaddr *)&address, &address_len) != 0 ||
        !socket_format_address(&address,
                               entry->local_ip,
                               sizeof(entry->local_ip),
                               &entry->local_port)) {
        snprintf(entry->local_ip, sizeof(entry->local_ip), "0.0.0.0");
    }
}

static int socket_wait_fd(int fd, bool writable, int timeout_ms)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;

    for (;;) {
        fd_set read_fds;
        fd_set write_fds;
        struct timeval poll_timeout = {0};
        int result;

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        if (writable) {
            FD_SET(fd, &write_fds);
        } else {
            FD_SET(fd, &read_fds);
        }
        result = select(fd + 1,
                        writable ? NULL : &read_fds,
                        writable ? &write_fds : NULL,
                        NULL,
                        &poll_timeout);
        if (result > 0) {
            return 1;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        if (timeout_ms == 0 || esp_timer_get_time() >= deadline_us) {
            return 0;
        }
        {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            uint32_t delay_ms = remaining_us > (int64_t)SOCKET_WAIT_SLICE_MS * 1000LL
                                    ? SOCKET_WAIT_SLICE_MS
                                    : (uint32_t)((remaining_us + 999LL) / 1000LL);

            if (delay_ms == 0) {
                return 0;
            }
            if (!esp32_mquickjs_cooperative_delay(runtime, delay_ms)) {
                return -2;
            }
        }
    }
}

static JSValue socket_wait_failure(JSContext *ctx, int wait_result, const char *operation)
{
    if (wait_result == -2) {
        return JS_ThrowInternalError(ctx, "sys.withTimeout() deadline exceeded");
    }
    return JS_ThrowInternalError(ctx, "%s failed: errno=%d", operation, errno);
}

static bool socket_resolve(const char *host,
                           int port,
                           int socktype,
                           struct addrinfo **out_addresses)
{
    struct addrinfo hints = {0};
    char service[6];

    snprintf(service, sizeof(service), "%d", port);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    return getaddrinfo(host, service, &hints, out_addresses) == 0;
}

static bool socket_get_host(JSContext *ctx,
                            JSValue value,
                            char *host,
                            size_t host_size)
{
    JSCStringBuf host_buf;
    const char *raw_host;
    size_t host_len = 0;

    if (!JS_IsString(ctx, value)) {
        return false;
    }
    raw_host = JS_ToCStringLen(ctx, &host_len, value, &host_buf);
    if (raw_host == NULL || host_len == 0 || host_len >= host_size) {
        return false;
    }
    memcpy(host, raw_host, host_len);
    host[host_len] = '\0';
    return true;
}

bool esp32_mquickjs_init_socket_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    size_t i;

    (void)ctx;
    (void)runtime;
    if (s_socket.initialized) {
        return true;
    }
    memset(&s_socket, 0, sizeof(s_socket));
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES; ++i) {
        s_socket.entries[i].fd = -1;
    }
    s_socket.initialized = true;
    return true;
}

void esp32_mquickjs_deinit_socket_runtime(JSContext *ctx)
{
    size_t i;

    (void)ctx;
    if (!s_socket.initialized) {
        return;
    }
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES; ++i) {
        socket_close_entry(&s_socket.entries[i]);
    }
    memset(&s_socket, 0, sizeof(s_socket));
}

JSValue js_socket_open(JSContext *ctx,
                       JSValue *this_val,
                       int argc,
                       JSValue *argv)
{
    JSCStringBuf protocol_buf;
    const char *protocol_name;
    socket_protocol_t protocol;
    socket_entry_t *entry;
    struct sockaddr_in local_address = {0};
    int local_port;
    int fd;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0]) ||
        !socket_optional_int(ctx, argc, argv, 1, 0, 0, 65535, &local_port)) {
        return JS_ThrowTypeError(ctx,
                                 "socket.open(protocol, local_port) expects tcp or udp and a valid port");
    }
    protocol_name = JS_ToCString(ctx, argv[0], &protocol_buf);
    if (protocol_name == NULL) {
        return JS_EXCEPTION;
    }
    if (strcmp(protocol_name, "tcp") == 0) {
        protocol = SOCKET_PROTOCOL_TCP;
    } else if (strcmp(protocol_name, "udp") == 0) {
        protocol = SOCKET_PROTOCOL_UDP;
    } else {
        return JS_ThrowRangeError(ctx, "socket protocol must be tcp or udp");
    }
    entry = socket_allocate_entry();
    if (entry == NULL) {
        return JS_ThrowInternalError(ctx, "socket handle limit reached");
    }
    fd = socket(AF_INET,
                protocol == SOCKET_PROTOCOL_TCP ? SOCK_STREAM : SOCK_DGRAM,
                0);
    if (fd < 0 || !socket_set_nonblocking(fd)) {
        if (fd >= 0) {
            close(fd);
        }
        socket_reset_entry(entry);
        return JS_ThrowInternalError(ctx, "socket.open() failed: errno=%d", errno);
    }
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons((uint16_t)local_port);
    if (bind(fd, (struct sockaddr *)&local_address, sizeof(local_address)) != 0) {
        close(fd);
        socket_reset_entry(entry);
        return JS_ThrowInternalError(ctx, "socket.open() bind failed: errno=%d", errno);
    }
    entry->fd = fd;
    entry->protocol = protocol;
    socket_refresh_local_address(entry);
    return JS_NewInt32(ctx, entry->id);
}

JSValue js_socket_close(JSContext *ctx,
                        JSValue *this_val,
                        int argc,
                        JSValue *argv)
{
    socket_entry_t *entry;
    int id;

    (void)this_val;
    if (argc < 1 || !socket_to_int(ctx, argv[0], 1, INT32_MAX, &id)) {
        return JS_ThrowTypeError(ctx, "socket.close(socket_id) expects a socket id");
    }
    entry = socket_find_entry(id);
    if (entry == NULL) {
        return JS_NewBool(false);
    }
    socket_close_entry(entry);
    return JS_NewBool(true);
}

JSValue js_socket_status(JSContext *ctx,
                         JSValue *this_val,
                         int argc,
                         JSValue *argv)
{
    socket_entry_t *entry;
    JSGCRef status_ref;
    JSValue *status;

    (void)this_val;
    if (argc < 1 || (entry = socket_require_entry(ctx,
                                                  argv[0],
                                                  SOCKET_PROTOCOL_NONE)) == NULL) {
        return JS_EXCEPTION;
    }
    socket_refresh_local_address(entry);
    status = JS_PushGCRef(ctx, &status_ref);
    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "id", JS_NewInt32(ctx, entry->id)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "protocol",
                                         JS_NewString(ctx, socket_protocol_name(entry->protocol))) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "connected",
                                         JS_NewBool(entry->connected)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "listening",
                                         JS_NewBool(entry->listening)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "peerClosed",
                                         JS_NewBool(entry->peer_closed)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "localIp",
                                         JS_NewString(ctx, entry->local_ip)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "localPort",
                                         JS_NewInt32(ctx, entry->local_port)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "remoteIp",
                                         JS_NewString(ctx, entry->remote_ip)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "remotePort",
                                         JS_NewInt32(ctx, entry->remote_port)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "sentBytes",
                                         JS_NewInt32(ctx, (int32_t)entry->sent_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                         status,
                                         "receivedBytes",
                                         JS_NewInt32(ctx, (int32_t)entry->received_bytes))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &status_ref);
}

JSValue js_socket_get_max_message_bytes(JSContext *ctx,
                                        JSValue *this_val,
                                        int argc,
                                        JSValue *argv)
{
    (void)this_val;
    if (argc < 1 || socket_require_entry(ctx,
                                         argv[0],
                                         SOCKET_PROTOCOL_NONE) == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(ctx, CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES);
}

JSValue js_socket_tcp_connect(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    socket_entry_t *entry;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char host[SOCKET_HOST_MAX_BYTES + 1U];
    int port;
    int timeout_ms;
    int connect_error = ECONNREFUSED;
    bool connected = false;

    (void)this_val;
    if (argc < 3 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_TCP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (entry->connected || entry->listening) {
        return JS_ThrowInternalError(ctx, "TCP socket is already active");
    }
    if (!socket_get_host(ctx, argv[1], host, sizeof(host)) ||
        !socket_to_int(ctx, argv[2], 1, 65535, &port) ||
        !socket_optional_int(ctx,
                             argc,
                             argv,
                             3,
                             SOCKET_DEFAULT_CONNECT_TIMEOUT_MS,
                             0,
                             SOCKET_MAX_TIMEOUT_MS,
                             &timeout_ms)) {
        return JS_ThrowTypeError(ctx,
                                 "socket.tcp.connect(socket_id, remote_ip, remote_port, timeout) expected");
    }
    if (!socket_resolve(host, port, SOCK_STREAM, &addresses)) {
        return JS_ThrowInternalError(ctx, "socket.tcp.connect() could not resolve remote_ip");
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        int result = connect(entry->fd, address->ai_addr, address->ai_addrlen);

        if (result == 0) {
            connected = true;
        } else if (errno == EINPROGRESS || errno == EALREADY) {
            int wait_result = socket_wait_fd(entry->fd, true, timeout_ms);

            if (wait_result == -2) {
                freeaddrinfo(addresses);
                return socket_wait_failure(ctx, wait_result, "socket.tcp.connect()");
            }
            if (wait_result > 0) {
                socklen_t error_len = sizeof(connect_error);

                if (getsockopt(entry->fd,
                               SOL_SOCKET,
                               SO_ERROR,
                               &connect_error,
                               &error_len) == 0 && connect_error == 0) {
                    connected = true;
                }
            }
        } else {
            connect_error = errno;
        }
        if (connected) {
            break;
        }
    }
    freeaddrinfo(addresses);
    if (!connected) {
        errno = connect_error;
        return JS_ThrowInternalError(ctx,
                                     "socket.tcp.connect() failed: errno=%d",
                                     connect_error);
    }
    entry->connected = true;
    entry->peer_closed = false;
    entry->remote_port = port;
    snprintf(entry->remote_ip, sizeof(entry->remote_ip), "%s", host);
    socket_refresh_local_address(entry);
    return JS_NewBool(true);
}

JSValue js_socket_tcp_listen(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    socket_entry_t *entry;
    int backlog;

    (void)this_val;
    if (argc < 1 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_TCP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (entry->connected) {
        return JS_ThrowInternalError(ctx, "connected TCP socket cannot listen");
    }
    if (!socket_optional_int(ctx,
                             argc,
                             argv,
                             1,
                             SOCKET_DEFAULT_LISTEN_BACKLOG,
                             1,
                             CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES,
                             &backlog)) {
        return JS_ThrowRangeError(ctx, "invalid socket.tcp.listen() backlog");
    }
    if (listen(entry->fd, backlog) != 0) {
        return JS_ThrowInternalError(ctx, "socket.tcp.listen() failed: errno=%d", errno);
    }
    entry->listening = true;
    return JS_NewBool(true);
}

JSValue js_socket_tcp_accept(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    socket_entry_t *listener;
    socket_entry_t *client;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_len = sizeof(remote_address);
    int timeout_ms;
    int wait_result;
    int client_fd;

    (void)this_val;
    if (argc < 1 ||
        (listener = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_TCP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (!listener->listening) {
        return JS_ThrowInternalError(ctx, "TCP socket is not listening");
    }
    if (!socket_optional_int(ctx,
                             argc,
                             argv,
                             1,
                             0,
                             0,
                             SOCKET_MAX_TIMEOUT_MS,
                             &timeout_ms)) {
        return JS_ThrowRangeError(ctx, "invalid socket.tcp.accept() timeout");
    }
    wait_result = socket_wait_fd(listener->fd, false, timeout_ms);
    if (wait_result < 0) {
        return socket_wait_failure(ctx, wait_result, "socket.tcp.accept()");
    }
    if (wait_result == 0) {
        return JS_NULL;
    }
    client_fd = accept(listener->fd,
                       (struct sockaddr *)&remote_address,
                       &remote_address_len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return JS_NULL;
        }
        return JS_ThrowInternalError(ctx, "socket.tcp.accept() failed: errno=%d", errno);
    }
    client = socket_allocate_entry();
    if (client == NULL || !socket_set_nonblocking(client_fd)) {
        close(client_fd);
        if (client != NULL) {
            socket_reset_entry(client);
        }
        return JS_ThrowInternalError(ctx, "socket handle limit reached");
    }
    client->fd = client_fd;
    client->protocol = SOCKET_PROTOCOL_TCP;
    client->connected = true;
    socket_refresh_local_address(client);
    if (!socket_format_address(&remote_address,
                               client->remote_ip,
                               sizeof(client->remote_ip),
                               &client->remote_port)) {
        snprintf(client->remote_ip, sizeof(client->remote_ip), "unknown");
    }
    return JS_NewInt32(ctx, client->id);
}

JSValue js_socket_tcp_send(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    socket_entry_t *entry;
    JSCStringBuf data_buf;
    const char *data;
    size_t data_len = 0;
    size_t offset = 0;
    int timeout_ms;

    (void)this_val;
    if (argc < 2 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_TCP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (!entry->connected) {
        return JS_ThrowInternalError(ctx, "TCP socket is not connected");
    }
    if (!JS_IsString(ctx, argv[1]) ||
        !socket_optional_int(ctx,
                             argc,
                             argv,
                             2,
                             0,
                             0,
                             SOCKET_MAX_TIMEOUT_MS,
                             &timeout_ms)) {
        return JS_ThrowTypeError(ctx,
                                 "socket.tcp.send(socket_id, data, timeout) expects string data");
    }
    data = JS_ToCStringLen(ctx, &data_len, argv[1], &data_buf);
    if (data == NULL) {
        return JS_EXCEPTION;
    }
    if (data_len > CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES) {
        return JS_ThrowRangeError(ctx, "socket.tcp.send() data exceeds socket limit");
    }
    while (offset < data_len) {
        int wait_result = socket_wait_fd(entry->fd, true, timeout_ms);
        ssize_t sent;

        if (wait_result < 0) {
            if (offset > 0) {
                break;
            }
            return socket_wait_failure(ctx, wait_result, "socket.tcp.send()");
        }
        if (wait_result == 0) {
            break;
        }
        sent = send(entry->fd, data + offset, data_len - offset, 0);
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (timeout_ms == 0) {
                break;
            }
            continue;
        }
        if (sent <= 0) {
            entry->connected = false;
            entry->peer_closed = true;
            if (offset == 0) {
                return JS_ThrowInternalError(ctx,
                                             "socket.tcp.send() failed: errno=%d",
                                             errno);
            }
            break;
        }
        offset += (size_t)sent;
    }
    entry->sent_bytes += (uint32_t)offset;
    return JS_NewInt32(ctx, (int32_t)offset);
}

JSValue js_socket_tcp_recv(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    socket_entry_t *entry;
    char *data;
    int max_bytes;
    int timeout_ms;
    int wait_result;
    ssize_t received;

    (void)this_val;
    if (argc < 1 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_TCP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (!entry->connected) {
        return JS_NULL;
    }
    if (!socket_optional_int(ctx,
                             argc,
                             argv,
                             1,
                             CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES,
                             1,
                             CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES,
                             &max_bytes) ||
        !socket_optional_int(ctx,
                             argc,
                             argv,
                             2,
                             0,
                             0,
                             SOCKET_MAX_TIMEOUT_MS,
                             &timeout_ms)) {
        return JS_ThrowRangeError(ctx, "invalid socket.tcp.recv() option");
    }
    wait_result = socket_wait_fd(entry->fd, false, timeout_ms);
    if (wait_result < 0) {
        return socket_wait_failure(ctx, wait_result, "socket.tcp.recv()");
    }
    if (wait_result == 0) {
        return JS_NULL;
    }
    data = heap_caps_malloc((size_t)max_bytes, MALLOC_CAP_8BIT);
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    received = recv(entry->fd, data, (size_t)max_bytes, 0);
    if (received == 0) {
        entry->connected = false;
        entry->peer_closed = true;
        heap_caps_free(data);
        return JS_NULL;
    }
    if (received < 0) {
        heap_caps_free(data);
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return JS_NULL;
        }
        entry->connected = false;
        return JS_ThrowInternalError(ctx, "socket.tcp.recv() failed: errno=%d", errno);
    }
    entry->received_bytes += (uint32_t)received;
    {
        JSValue result = JS_NewStringLen(ctx, data, (size_t)received);

        heap_caps_free(data);
        return result;
    }
}

JSValue js_socket_udp_sendto(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    socket_entry_t *entry;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    JSCStringBuf data_buf;
    char host[SOCKET_HOST_MAX_BYTES + 1U];
    const char *data;
    size_t data_len = 0;
    int port;
    ssize_t sent = -1;

    (void)this_val;
    if (argc < 4 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_UDP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (!socket_get_host(ctx, argv[1], host, sizeof(host)) ||
        !socket_to_int(ctx, argv[2], 1, 65535, &port) ||
        !JS_IsString(ctx, argv[3])) {
        return JS_ThrowTypeError(ctx,
                                 "socket.udp.sendto(socket_id, remote_ip, remote_port, data) expected");
    }
    data = JS_ToCStringLen(ctx, &data_len, argv[3], &data_buf);
    if (data == NULL) {
        return JS_EXCEPTION;
    }
    if (data_len > CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES) {
        return JS_ThrowRangeError(ctx, "socket.udp.sendto() data exceeds socket limit");
    }
    if (!socket_resolve(host, port, SOCK_DGRAM, &addresses)) {
        return JS_ThrowInternalError(ctx, "socket.udp.sendto() could not resolve remote_ip");
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        sent = sendto(entry->fd,
                      data,
                      data_len,
                      0,
                      address->ai_addr,
                      address->ai_addrlen);
        if (sent == (ssize_t)data_len) {
            break;
        }
    }
    freeaddrinfo(addresses);
    if (sent != (ssize_t)data_len) {
        return JS_ThrowInternalError(ctx, "socket.udp.sendto() failed: errno=%d", errno);
    }
    entry->sent_bytes += (uint32_t)sent;
    return JS_NewInt32(ctx, (int32_t)sent);
}

JSValue js_socket_udp_recvfrom(JSContext *ctx,
                               JSValue *this_val,
                               int argc,
                               JSValue *argv)
{
    socket_entry_t *entry;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_len = sizeof(remote_address);
    char remote_ip[SOCKET_HOST_MAX_BYTES + 1U];
    char *data;
    int remote_port = 0;
    int max_bytes;
    int timeout_ms;
    int wait_result;
    ssize_t received;

    (void)this_val;
    if (argc < 1 ||
        (entry = socket_require_entry(ctx, argv[0], SOCKET_PROTOCOL_UDP)) == NULL) {
        return JS_EXCEPTION;
    }
    if (!socket_optional_int(ctx,
                             argc,
                             argv,
                             1,
                             CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES,
                             1,
                             CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES,
                             &max_bytes) ||
        !socket_optional_int(ctx,
                             argc,
                             argv,
                             2,
                             0,
                             0,
                             SOCKET_MAX_TIMEOUT_MS,
                             &timeout_ms)) {
        return JS_ThrowRangeError(ctx, "invalid socket.udp.recvfrom() option");
    }
    wait_result = socket_wait_fd(entry->fd, false, timeout_ms);
    if (wait_result < 0) {
        return socket_wait_failure(ctx, wait_result, "socket.udp.recvfrom()");
    }
    if (wait_result == 0) {
        return JS_NULL;
    }
    data = heap_caps_malloc((size_t)max_bytes, MALLOC_CAP_8BIT);
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    received = recvfrom(entry->fd,
                        data,
                        (size_t)max_bytes,
                        0,
                        (struct sockaddr *)&remote_address,
                        &remote_address_len);
    if (received < 0) {
        heap_caps_free(data);
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return JS_NULL;
        }
        return JS_ThrowInternalError(ctx, "socket.udp.recvfrom() failed: errno=%d", errno);
    }
    if (!socket_format_address(&remote_address,
                               remote_ip,
                               sizeof(remote_ip),
                               &remote_port)) {
        snprintf(remote_ip, sizeof(remote_ip), "unknown");
    }
    entry->received_bytes += (uint32_t)received;
    {
        JSGCRef result_ref;
        JSValue *result = JS_PushGCRef(ctx, &result_ref);

        *result = JS_NewObject(ctx);
        if (JS_IsException(*result) ||
            !esp32_mquickjs_set_property_ref(ctx,
                                             result,
                                             "data",
                                             JS_NewStringLen(ctx,
                                                             data,
                                                             (size_t)received)) ||
            !esp32_mquickjs_set_property_ref(ctx,
                                             result,
                                             "remoteIp",
                                             JS_NewString(ctx, remote_ip)) ||
            !esp32_mquickjs_set_property_ref(ctx,
                                             result,
                                             "remotePort",
                                             JS_NewInt32(ctx, remote_port))) {
            heap_caps_free(data);
            JS_PopGCRef(ctx, &result_ref);
            return JS_EXCEPTION;
        }
        heap_caps_free(data);
        return JS_PopGCRef(ctx, &result_ref);
    }
}

#endif
