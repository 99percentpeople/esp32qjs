#include "esp32_mquickjs_socket.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "utils/esp32_mquickjs_byte_source.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
#include "utils/esp32_mquickjs_tls_error.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#endif
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define SOCKET_HOST_MAX_BYTES 253U
#define SOCKET_POLL_INTERVAL_US 10000U
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
    bool busy;
    bool secure;
    int local_port;
    int remote_port;
    char local_ip[INET6_ADDRSTRLEN];
    char remote_host[SOCKET_HOST_MAX_BYTES + 1U];
    uint32_t sent_bytes;
    uint32_t received_bytes;
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    esp_tls_t *tls;
#endif
} socket_entry_t;

typedef struct {
    bool initialized;
    int next_id;
    socket_entry_t entries[CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES];
} socket_state_t;

static socket_state_t s_socket;

static bool socket_register_future_drivers(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime);

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

#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
static void socket_close_tls_connection(socket_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->tls != NULL) {
        (void)esp_tls_conn_destroy(entry->tls);
        entry->tls = NULL;
    }
    entry->fd = -1;
    entry->connected = false;
}
#endif

static void socket_close_entry(socket_entry_t *entry)
{
    if (entry == NULL || entry->id == 0) {
        return;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (entry->tls != NULL) {
        socket_close_tls_connection(entry);
    } else
#endif
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

static bool socket_open_options(JSContext *ctx,
                                int argc,
                                JSValue *argv,
                                int *out_local_port,
                                bool *out_secure)
{
    JSGCRef option_ref;
    JSValue *option;
    bool valid = true;

    *out_local_port = 0;
    *out_secure = false;
    if (argc < 2 || JS_IsUndefined(argv[1])) {
        return true;
    }
    if (JS_GetClassID(ctx, argv[1]) < 0 || JS_IsArray(ctx, argv[1])) {
        return false;
    }

    option = JS_PushGCRef(ctx, &option_ref);
    *option = JS_GetPropertyStr(ctx, argv[1], "localPort");
    if (JS_IsException(*option) ||
        (!JS_IsUndefined(*option) &&
         !socket_to_int(ctx, *option, 0, 65535, out_local_port))) {
        valid = false;
    }
    if (valid) {
        *option = JS_GetPropertyStr(ctx, argv[1], "tls");
        if (JS_IsException(*option) ||
            (!JS_IsUndefined(*option) && !JS_IsBool(*option))) {
            valid = false;
        } else if (!JS_IsUndefined(*option)) {
            *out_secure = *option == JS_TRUE;
        }
    }
    JS_PopGCRef(ctx, &option_ref);
    return valid;
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

static int socket_poll_fd(int fd, bool writable)
{
    fd_set read_fds;
    fd_set write_fds;
    struct timeval timeout = {0};

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (writable) {
        FD_SET(fd, &write_fds);
    } else {
        FD_SET(fd, &read_fds);
    }
    return select(fd + 1,
                  writable ? NULL : &read_fds,
                  writable ? &write_fds : NULL,
                  NULL,
                  &timeout);
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

    if (s_socket.initialized) {
        return true;
    }
    memset(&s_socket, 0, sizeof(s_socket));
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_SOCKET_MAX_HANDLES; ++i) {
        s_socket.entries[i].fd = -1;
    }
    s_socket.initialized = true;
    if (!socket_register_future_drivers(ctx, runtime)) {
        memset(&s_socket, 0, sizeof(s_socket));
        return false;
    }
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
    bool secure;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0]) ||
        !socket_open_options(ctx, argc, argv, &local_port, &secure)) {
        return JS_ThrowTypeError(ctx,
                                 "socket.open(protocol, options) expects tcp or udp and an options object");
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
#if !CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (secure) {
        return JS_ThrowTypeError(ctx,
                                 "TLS sockets require the TLS firmware capability");
    }
#else
    if (secure && protocol != SOCKET_PROTOCOL_TCP) {
        return JS_ThrowTypeError(ctx, "TLS is only supported for TCP client sockets");
    }
    if (secure && local_port != 0) {
        return JS_ThrowRangeError(ctx, "TLS client sockets do not support a fixed localPort");
    }
#if !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) || !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    if (secure) {
        return JS_ThrowInternalError(ctx,
                                     "TLS socket support requires the certificate bundle");
    }
#endif
#endif
    entry = socket_allocate_entry();
    if (entry == NULL) {
        return JS_ThrowInternalError(ctx, "socket handle limit reached");
    }
    if (secure) {
        entry->protocol = SOCKET_PROTOCOL_TCP;
        entry->secure = true;
        snprintf(entry->local_ip, sizeof(entry->local_ip), "0.0.0.0");
        return JS_NewInt32(ctx, entry->id);
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
    if (entry->busy) {
        return JS_ThrowInternalError(ctx,
                                     "socket.close() refused while an operation is pending");
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
                                         "secure",
                                         JS_NewBool(entry->secure)) ||
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
                                         "remoteHost",
                                         JS_NewString(ctx, entry->remote_host)) ||
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

JSValue js_socket_get_max_transfer_bytes(JSContext *ctx,
                                         JSValue *this_val,
                                         int argc,
                                         JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES);
}

static JSValue socket_future_call_and_wait(JSContext *ctx,
                                           const char *family_name,
                                           const char *method_name,
                                           int argc,
                                           JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef socket_ref;
    JSGCRef family_ref;
    JSGCRef method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *socket = JS_PushGCRef(ctx, &socket_ref);
    JSValue *family = JS_PushGCRef(ctx, &family_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *global = JS_GetGlobalObject(ctx);
    *socket = JS_IsException(*global)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *global, "socket");
    *family = JS_IsException(*socket)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *socket, family_name);
    *method = JS_IsException(*family)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *family, method_name);
    if (JS_IsException(*global) || JS_IsException(*socket) ||
        JS_IsException(*family) || JS_IsException(*method)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(ctx,
                                                     esp32_mquickjs_get_active_runtime(),
                                                     *method,
                                                     *family,
                                                     argc,
                                                     argv);
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &family_ref);
    JS_PopGCRef(ctx, &socket_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

typedef enum {
    SOCKET_FUTURE_TCP_CONNECT,
    SOCKET_FUTURE_TCP_ACCEPT,
    SOCKET_FUTURE_TCP_SEND,
    SOCKET_FUTURE_TCP_RECV,
    SOCKET_FUTURE_UDP_SENDTO,
    SOCKET_FUTURE_UDP_RECVFROM,
} socket_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    socket_future_kind_t kind;
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t poll_timer;
    int entry_id;
    int fd;
    int port;
    int timeout_ms;
    int error_code;
    int client_fd;
    int remote_port;
    uint64_t deadline_us;
    size_t length;
    size_t offset;
    ssize_t received;
    uint8_t *data;
    const uint8_t *send_data;
    JSGCRef send_owner_ref;
    esp32_mquickjs_byte_span_source_t send_source;
    esp32_mquickjs_byte_span_t send_span;
    size_t send_span_offset;
    size_t source_produced;
    struct sockaddr_storage address;
    socklen_t address_len;
    char host[SOCKET_HOST_MAX_BYTES + 1U];
    char error_text[96];
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    esp_tls_cfg_t tls_config;
    esp32_mquickjs_tls_error_t tls_error;
#endif
    bool started;
    bool issued;
    bool empty_result;
    bool completed;
    bool cancelled;
    bool send_owner_rooted;
    bool send_source_open;
    bool send_is_span_source;
    bool source_has_known_length;
};

static socket_entry_t *socket_future_entry(
    const esp32_mquickjs_future_driver_state_t *state)
{
    socket_entry_t *entry = state != NULL ? socket_find_entry(state->entry_id) : NULL;

    return entry != NULL && entry->fd == state->fd ? entry : NULL;
}

static void socket_future_fail(esp32_mquickjs_future_driver_state_t *state,
                               int error_code,
                               const char *message)
{
    if (state == NULL || state->completed) {
        return;
    }
    state->error_code = error_code != 0 ? error_code : EIO;
    if (message != NULL) {
        snprintf(state->error_text, sizeof(state->error_text), "%s", message);
    }
    state->completed = true;
}

static bool socket_future_copy_source(JSContext *ctx,
                                      JSValue value,
                                      const char *api_name,
                                      esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue source_error = JS_UNDEFINED;

    if (!esp32_mquickjs_get_byte_source(ctx,
                                       value,
                                       api_name,
                                       &source,
                                       &owned,
                                       &source_error)) {
        if (!JS_IsException(source_error)) {
            (void)JS_Throw(ctx, source_error);
        }
        return false;
    }
    if (source.length > CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES) {
        esp32_mquickjs_release_byte_source(owned);
        JS_ThrowRangeError(ctx, "%s data exceeds socket limit", api_name);
        return false;
    }
    if (source.length > 0) {
        state->data = esp32_mquickjs_memory_payload_alloc(
            source.length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (state->data == NULL) {
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(state->data, source.data, source.length);
    }
    state->length = source.length;
    esp32_mquickjs_release_byte_source(owned);
    return true;
}

static void socket_future_release_send_source(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    esp32_mquickjs_byte_span_clear(&state->send_span);
    if (state->send_source_open) {
        esp32_mquickjs_byte_span_source_close(state->ctx,
                                              &state->send_source);
        state->send_source_open = false;
    }
    if (state->send_owner_rooted) {
        JS_DeleteGCRef(state->ctx, &state->send_owner_ref);
        state->send_owner_rooted = false;
    }
}

static bool socket_future_prepare_tcp_source(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_future_driver_state_t *state)
{
    int class_id = JS_GetClassID(ctx, value);

    if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
        class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
        JSValue source_error = JS_UNDEFINED;
        size_t known_length = 0;

        state->source_has_known_length =
            esp32_mquickjs_byte_span_source_known_length(
                ctx, value, &known_length);
        if (state->source_has_known_length &&
            known_length > CONFIG_ESP32_MQUICKJS_SOCKET_MAX_SOURCE_BYTES) {
            JS_ThrowRangeError(ctx, "%s source exceeds socket stream limit",
                               api_name);
            return false;
        }
        if (!esp32_mquickjs_open_byte_span_source(
                ctx, value, api_name, &state->send_source, &source_error)) {
            if (!JS_IsUndefined(source_error) &&
                !JS_IsException(source_error)) {
                (void)JS_Throw(ctx, source_error);
            }
            return false;
        }
        state->send_source_open = true;
        state->send_is_span_source = true;
        state->length = known_length;
        *JS_AddGCRef(ctx, &state->send_owner_ref) = value;
        state->send_owner_rooted = true;
        return true;
    }

    {
        esp32_mquickjs_byte_source_t source;
        uint8_t *owned = NULL;
        JSValue source_error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(ctx, value, api_name, &source,
                                            &owned, &source_error)) {
            if (!JS_IsUndefined(source_error) &&
                !JS_IsException(source_error)) {
                (void)JS_Throw(ctx, source_error);
            }
            return false;
        }
        if (source.length > CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES) {
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowRangeError(ctx, "%s data exceeds socket limit", api_name);
            return false;
        }
        state->data = owned;
        state->send_data = source.data;
        state->length = source.length;
        if (owned == NULL && source.length > 0) {
            *JS_AddGCRef(ctx, &state->send_owner_ref) = value;
            state->send_owner_rooted = true;
        }
        return true;
    }
}

static esp32_mquickjs_future_driver_state_t *socket_future_allocate(
    JSContext *ctx,
    JSValue socket_id,
    socket_protocol_t protocol,
    socket_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    socket_entry_t *entry = socket_require_entry(ctx, socket_id, protocol);

    if (entry == NULL) {
        return NULL;
    }
    if (entry->busy) {
        JS_ThrowInternalError(ctx, "socket already has an operation in progress");
        return NULL;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    state->kind = kind;
    state->ctx = ctx;
    state->entry_id = entry->id;
    state->fd = entry->fd;
    state->client_fd = -1;
    state->received = -1;
    esp32_mquickjs_byte_span_clear(&state->send_span);
    return state;
}

static bool socket_tcp_connect_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    socket_entry_t *entry;

    (void)this_ref;
    if (out_state == NULL || argc < 3 || argc > 4) {
        JS_ThrowTypeError(ctx,
                          "socket.tcp.connect(socketId, remoteHost, remotePort, timeoutMs?) expected");
        return false;
    }
    state = socket_future_allocate(ctx, argv[0].val, SOCKET_PROTOCOL_TCP,
                                   SOCKET_FUTURE_TCP_CONNECT);
    if (state == NULL) {
        return false;
    }
    entry = socket_future_entry(state);
    if (entry == NULL || entry->connected || entry->listening ||
        !socket_get_host(ctx, argv[1].val, state->host, sizeof(state->host)) ||
        !socket_to_int(ctx, argv[2].val, 1, 65535, &state->port) ||
        (argc >= 4 && !JS_IsUndefined(argv[3].val) &&
         !socket_to_int(ctx, argv[3].val, 0, SOCKET_MAX_TIMEOUT_MS,
                        &state->timeout_ms))) {
        heap_caps_free(state);
        if (entry != NULL && (entry->connected || entry->listening)) {
            JS_ThrowInternalError(ctx, "TCP socket is already active");
        } else {
            JS_ThrowTypeError(ctx,
                              "socket.tcp.connect(socketId, remoteHost, remotePort, timeoutMs?) expected");
        }
        return false;
    }
    if (argc < 4 || JS_IsUndefined(argv[3].val)) {
        state->timeout_ms = SOCKET_DEFAULT_CONNECT_TIMEOUT_MS;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (entry->secure) {
        state->tls_config.non_block = true;
        state->tls_config.timeout_ms = state->timeout_ms;
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        state->tls_config.crt_bundle_attach =
            esp32_mquickjs_tls_crt_bundle_attach;
#endif
    }
#endif
    *out_state = state;
    return true;
}

static bool socket_tcp_accept_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    socket_entry_t *entry;

    (void)this_ref;
    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(ctx, "socket.tcp.accept(socketId, timeoutMs?) expected");
        return false;
    }
    state = socket_future_allocate(ctx, argv[0].val, SOCKET_PROTOCOL_TCP,
                                   SOCKET_FUTURE_TCP_ACCEPT);
    if (state == NULL) {
        return false;
    }
    entry = socket_future_entry(state);
    if (entry == NULL || !entry->listening ||
        (argc >= 2 && !JS_IsUndefined(argv[1].val) &&
         !socket_to_int(ctx, argv[1].val, 0, SOCKET_MAX_TIMEOUT_MS,
                        &state->timeout_ms))) {
        heap_caps_free(state);
        if (entry != NULL && !entry->listening) {
            JS_ThrowInternalError(ctx, "TCP socket is not listening");
        } else {
            JS_ThrowRangeError(ctx, "invalid socket.tcp.accept() timeout");
        }
        return false;
    }
    *out_state = state;
    return true;
}

static bool socket_tcp_send_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    socket_entry_t *entry;

    (void)this_ref;
    if (out_state == NULL || argc < 2 || argc > 3) {
        JS_ThrowTypeError(ctx,
                          "socket.tcp.send(socketId, data, timeoutMs?) expects ByteView or ByteSpanSource data");
        return false;
    }
    state = socket_future_allocate(ctx, argv[0].val, SOCKET_PROTOCOL_TCP,
                                   SOCKET_FUTURE_TCP_SEND);
    if (state == NULL) {
        return false;
    }
    entry = socket_future_entry(state);
    if (entry == NULL || !entry->connected ||
        (argc >= 3 && !JS_IsUndefined(argv[2].val) &&
         !socket_to_int(ctx, argv[2].val, 0, SOCKET_MAX_TIMEOUT_MS,
                        &state->timeout_ms)) ||
        !socket_future_prepare_tcp_source(
            ctx, argv[1].val,
            "socket.tcp.send(socketId, data, timeoutMs?)", state)) {
        socket_future_release_send_source(state);
        heap_caps_free(state->data);
        heap_caps_free(state);
        if (entry != NULL && !entry->connected) {
            JS_ThrowInternalError(ctx, "TCP socket is not connected");
        } else if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "socket.tcp.send(socketId, data, timeoutMs?) expects ByteView or ByteSpanSource data");
        }
        return false;
    }
    *out_state = state;
    return true;
}

static bool socket_receive_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    socket_protocol_t protocol,
    socket_future_kind_t kind,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    int max_bytes;

    (void)this_ref;
    if (out_state == NULL || argc < 1 || argc > 3) {
        JS_ThrowRangeError(ctx, "invalid socket receive option");
        return false;
    }
    state = socket_future_allocate(ctx, argv[0].val, protocol, kind);
    if (state == NULL) {
        return false;
    }
    max_bytes = CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES;
    if ((argc >= 2 && !JS_IsUndefined(argv[1].val) &&
         !socket_to_int(ctx, argv[1].val, 1,
                        CONFIG_ESP32_MQUICKJS_SOCKET_MAX_MESSAGE_BYTES,
                        &max_bytes)) ||
        (argc >= 3 && !JS_IsUndefined(argv[2].val) &&
         !socket_to_int(ctx, argv[2].val, 0, SOCKET_MAX_TIMEOUT_MS,
                        &state->timeout_ms))) {
        heap_caps_free(state);
        JS_ThrowRangeError(ctx, "invalid socket receive option");
        return false;
    }
    state->length = (size_t)max_bytes;
    state->data = esp32_mquickjs_memory_payload_alloc(
        state->length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (state->data == NULL) {
        heap_caps_free(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    *out_state = state;
    return true;
}

static bool socket_tcp_recv_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return socket_receive_future_prepare(ctx, this_ref, argc, argv,
                                         SOCKET_PROTOCOL_TCP,
                                         SOCKET_FUTURE_TCP_RECV,
                                         out_state);
}

static bool socket_udp_sendto_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc != 4) {
        JS_ThrowTypeError(ctx,
                          "socket.udp.sendto(socketId, remoteHost, remotePort, data) expected");
        return false;
    }
    state = socket_future_allocate(ctx, argv[0].val, SOCKET_PROTOCOL_UDP,
                                   SOCKET_FUTURE_UDP_SENDTO);
    if (state == NULL) {
        return false;
    }
    if (!socket_get_host(ctx, argv[1].val, state->host, sizeof(state->host)) ||
        !socket_to_int(ctx, argv[2].val, 1, 65535, &state->port) ||
        !socket_future_copy_source(ctx, argv[3].val,
                                   "socket.udp.sendto(socketId, remoteHost, remotePort, data)",
                                   state)) {
        heap_caps_free(state->data);
        heap_caps_free(state);
        JS_ThrowTypeError(ctx,
                          "socket.udp.sendto(socketId, remoteHost, remotePort, data) expected");
        return false;
    }
    *out_state = state;
    return true;
}

static bool socket_udp_recvfrom_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return socket_receive_future_prepare(ctx, this_ref, argc, argv,
                                         SOCKET_PROTOCOL_UDP,
                                         SOCKET_FUTURE_UDP_RECVFROM,
                                         out_state);
}

static void socket_future_poll_timer(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && !state->completed) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static void socket_future_stop_timer(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->poll_timer == NULL) {
        return;
    }
    (void)esp_timer_stop(state->poll_timer);
    (void)esp_timer_delete(state->poll_timer);
    state->poll_timer = NULL;
}

static bool socket_future_resolve_address(esp32_mquickjs_future_driver_state_t *state,
                                          int socktype)
{
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    bool found = false;

    if (!socket_resolve(state->host, state->port, socktype, &addresses)) {
        socket_future_fail(state, EHOSTUNREACH, "could not resolve remoteHost");
        return false;
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        if (address->ai_family != AF_INET ||
            address->ai_addrlen > sizeof(state->address)) {
            continue;
        }
        memcpy(&state->address, address->ai_addr, address->ai_addrlen);
        state->address_len = (socklen_t)address->ai_addrlen;
        found = true;
        break;
    }
    freeaddrinfo(addresses);
    if (!found) {
        socket_future_fail(state, EAFNOSUPPORT, "remoteHost has no IPv4 address");
    }
    return found;
}

static void socket_future_step_connect(esp32_mquickjs_future_driver_state_t *state,
                                       socket_entry_t *entry)
{
    int ready;
    int connect_error = 0;
    socklen_t error_len = sizeof(connect_error);

#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (entry->secure) {
        int result;
        int fd = -1;

        if (entry->tls == NULL) {
            entry->tls = esp_tls_init();
            if (entry->tls == NULL) {
                esp32_mquickjs_tls_error_set(
                    &state->tls_error, ESP_ERR_NO_MEM, ESP_OK, 0, 0);
                socket_future_fail(state, ENOMEM, "could not allocate TLS context");
                return;
            }
        }
        state->issued = true;
        result = esp_tls_conn_new_async(state->host,
                                        (int)strlen(state->host),
                                        state->port,
                                        &state->tls_config,
                                        entry->tls);
        if (result < 0) {
            esp32_mquickjs_tls_error_capture(
                &state->tls_error, entry->tls, ESP_FAIL);
            socket_future_fail(state, EIO, "TLS handshake failed");
        } else if (result > 0) {
            esp32_mquickjs_tls_error_merge_verify_flags(&state->tls_error);
            if (esp_tls_get_conn_sockfd(entry->tls, &fd) != ESP_OK || fd < 0) {
                socket_future_fail(state, EIO, "TLS socket descriptor unavailable");
                return;
            }
            entry->fd = fd;
            state->fd = fd;
            state->completed = true;
        }
        return;
    }
#endif

    if (!state->issued) {
        int result;

        state->issued = true;
        if (!socket_future_resolve_address(state, SOCK_STREAM)) {
            return;
        }
        result = connect(entry->fd,
                         (struct sockaddr *)&state->address,
                         state->address_len);
        if (result == 0 || (result < 0 && errno == EISCONN)) {
            state->completed = true;
            return;
        }
        if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR) {
            socket_future_fail(state, errno, NULL);
            return;
        }
    }
    ready = socket_poll_fd(entry->fd, true);
    if (ready < 0 && errno != EINTR) {
        socket_future_fail(state, errno, NULL);
    } else if (ready > 0 &&
               (getsockopt(entry->fd, SOL_SOCKET, SO_ERROR,
                           &connect_error, &error_len) != 0 || connect_error != 0)) {
        socket_future_fail(state,
                           connect_error != 0 ? connect_error : errno,
                           NULL);
    } else if (ready > 0) {
        state->completed = true;
    }
}

static void socket_future_step_accept(esp32_mquickjs_future_driver_state_t *state,
                                      socket_entry_t *entry)
{
    int ready = socket_poll_fd(entry->fd, false);

    if (ready < 0 && errno != EINTR) {
        socket_future_fail(state, errno, NULL);
        return;
    }
    if (ready <= 0) {
        return;
    }
    state->address_len = sizeof(state->address);
    state->client_fd = accept(entry->fd,
                              (struct sockaddr *)&state->address,
                              &state->address_len);
    if (state->client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        socket_future_fail(state, errno, NULL);
        return;
    }
    state->completed = true;
}

static bool socket_future_load_send_span(
    esp32_mquickjs_future_driver_state_t *state)
{
    unsigned empty_spans = 0;

    while (state->send_span_offset >= state->send_span.length) {
        esp32_mquickjs_byte_span_clear(&state->send_span);
        state->send_span_offset = 0;
        if (!esp32_mquickjs_byte_span_source_next(
                state->ctx, &state->send_source, &state->send_span)) {
            if (JS_HasException(state->ctx)) {
                (void)JS_GetException(state->ctx);
                socket_future_fail(state, EIO,
                                   "ByteSpanSource iteration failed");
            } else if (state->source_has_known_length &&
                       state->offset != state->length) {
                socket_future_fail(state, EIO,
                                   "ByteSpanSource length changed during send");
            } else {
                state->completed = true;
            }
            return false;
        }
        if (state->send_span.length == 0) {
            if (++empty_spans > 16) {
                socket_future_fail(state, EIO,
                                   "ByteSpanSource yielded too many empty spans");
                return false;
            }
            continue;
        }
        if (state->send_span.data == NULL ||
            state->send_span.length >
                CONFIG_ESP32_MQUICKJS_SOCKET_MAX_SOURCE_BYTES -
                    state->source_produced) {
            socket_future_fail(state, EFBIG,
                               "ByteSpanSource exceeds socket stream limit");
            return false;
        }
        if (state->source_has_known_length &&
            state->send_span.length > state->length - state->source_produced) {
            socket_future_fail(state, EIO,
                               "ByteSpanSource length changed during send");
            return false;
        }
        state->source_produced += state->send_span.length;
    }
    return true;
}

static void socket_future_step_send(esp32_mquickjs_future_driver_state_t *state,
                                    socket_entry_t *entry)
{
    const uint8_t *data;
    size_t remaining;
    ssize_t sent;
    int ready;

    if (state->send_is_span_source) {
        if (!socket_future_load_send_span(state)) {
            return;
        }
        data = state->send_span.data + state->send_span_offset;
        remaining = state->send_span.length - state->send_span_offset;
    } else {
        if (state->offset >= state->length) {
            state->completed = true;
            return;
        }
        data = state->send_data + state->offset;
        remaining = state->length - state->offset;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (entry->secure) {
        if (entry->tls == NULL) {
            socket_future_fail(state, EBADF, "TLS connection is unavailable");
            return;
        }
        sent = esp_tls_conn_write(entry->tls,
                                  data,
                                  remaining);
        if (sent == ESP_TLS_ERR_SSL_WANT_READ ||
            sent == ESP_TLS_ERR_SSL_WANT_WRITE) {
            return;
        }
    } else
#endif
    {
        ready = socket_poll_fd(entry->fd, true);
        if (ready < 0 && errno != EINTR) {
            socket_future_fail(state, errno, NULL);
            return;
        }
        if (ready <= 0) {
            return;
        }
        sent = send(entry->fd,
                    data,
                    remaining,
                    0);
        if (sent < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
    }
    if (sent <= 0) {
        entry->connected = false;
        entry->peer_closed = true;
        if (state->offset == 0) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
            if (entry->secure) {
                esp32_mquickjs_tls_error_capture(
                    &state->tls_error, entry->tls, ESP_FAIL);
            }
#endif
            socket_future_fail(state,
                               entry->secure ? EIO : errno,
                               entry->secure ? "TLS write failed" : NULL);
        } else {
            state->completed = true;
        }
        return;
    }
    state->offset += (size_t)sent;
    if (state->send_is_span_source) {
        state->send_span_offset += (size_t)sent;
    } else if (state->offset >= state->length) {
        state->completed = true;
    }
}

static void socket_future_step_receive(esp32_mquickjs_future_driver_state_t *state,
                                       socket_entry_t *entry,
                                       bool udp)
{
    int ready;

    if (!udp && !entry->connected) {
        state->empty_result = true;
        state->completed = true;
        return;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (!udp && entry->secure) {
        if (entry->tls == NULL) {
            socket_future_fail(state, EBADF, "TLS connection is unavailable");
            return;
        }
        state->received = esp_tls_conn_read(entry->tls,
                                            state->data,
                                            state->length);
        if (state->received == ESP_TLS_ERR_SSL_WANT_READ ||
            state->received == ESP_TLS_ERR_SSL_WANT_WRITE) {
            state->received = -1;
            return;
        }
    } else
#endif
    {
        ready = socket_poll_fd(entry->fd, false);
        if (ready < 0 && errno != EINTR) {
            socket_future_fail(state, errno, NULL);
            return;
        }
        if (ready <= 0) {
            return;
        }
    }
    if (udp) {
        state->address_len = sizeof(state->address);
        state->received = recvfrom(entry->fd,
                                   state->data,
                                   state->length,
                                   0,
                                   (struct sockaddr *)&state->address,
                                   &state->address_len);
    } else if (!entry->secure) {
        state->received = recv(entry->fd, state->data, state->length, 0);
    }
    if (state->received == 0 && !udp) {
        entry->connected = false;
        entry->peer_closed = true;
        state->empty_result = true;
        state->completed = true;
    } else if (!entry->secure && state->received < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        state->received = -1;
    } else if (state->received < 0) {
        if (!udp) {
            entry->connected = false;
        }
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        if (entry->secure) {
            esp32_mquickjs_tls_error_capture(
                &state->tls_error, entry->tls, ESP_FAIL);
        }
#endif
        socket_future_fail(state,
                           entry->secure ? EIO : errno,
                           entry->secure ? "TLS read failed" : NULL);
    } else {
        state->completed = true;
    }
}

static void socket_future_step_udp_sendto(
    esp32_mquickjs_future_driver_state_t *state,
    socket_entry_t *entry)
{
    ssize_t sent;

    state->issued = true;
    if (!socket_future_resolve_address(state, SOCK_DGRAM)) {
        return;
    }
    sent = sendto(entry->fd,
                  state->data,
                  state->length,
                  0,
                  (struct sockaddr *)&state->address,
                  state->address_len);
    if (sent != (ssize_t)state->length) {
        socket_future_fail(state, errno, NULL);
        return;
    }
    state->offset = (size_t)sent;
    state->completed = true;
}

static void socket_future_step(esp32_mquickjs_future_driver_state_t *state)
{
    socket_entry_t *entry;

    if (state == NULL || state->completed || state->cancelled) {
        return;
    }
    entry = socket_future_entry(state);
    if (entry == NULL) {
        socket_future_fail(state, EBADF, "socket was closed during operation");
        return;
    }
    switch (state->kind) {
        case SOCKET_FUTURE_TCP_CONNECT:
            socket_future_step_connect(state, entry);
            break;
        case SOCKET_FUTURE_TCP_ACCEPT:
            socket_future_step_accept(state, entry);
            break;
        case SOCKET_FUTURE_TCP_SEND:
            socket_future_step_send(state, entry);
            break;
        case SOCKET_FUTURE_TCP_RECV:
            socket_future_step_receive(state, entry, false);
            break;
        case SOCKET_FUTURE_UDP_SENDTO:
            socket_future_step_udp_sendto(state, entry);
            break;
        case SOCKET_FUTURE_UDP_RECVFROM:
            socket_future_step_receive(state, entry, true);
            break;
    }
    if (!state->completed && state->deadline_us > 0 &&
        (uint64_t)esp_timer_get_time() >= state->deadline_us) {
        if (state->kind == SOCKET_FUTURE_TCP_CONNECT) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
            if (entry->secure) {
                esp32_mquickjs_tls_error_capture(
                    &state->tls_error, entry->tls, ESP_ERR_TIMEOUT);
            }
#endif
            socket_future_fail(state, ETIMEDOUT, "timed out");
        } else if (state->kind == SOCKET_FUTURE_TCP_SEND) {
            state->completed = true;
        } else {
            state->empty_result = true;
            state->completed = true;
        }
    }
}

static void socket_future_complete_nonblocking(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->timeout_ms != 0) {
        return;
    }
    if (state->kind == SOCKET_FUTURE_TCP_CONNECT) {
        socket_future_fail(state, ETIMEDOUT, NULL);
    } else if (state->kind == SOCKET_FUTURE_TCP_SEND) {
        state->completed = true;
    } else if (state->kind == SOCKET_FUTURE_TCP_ACCEPT ||
               state->kind == SOCKET_FUTURE_TCP_RECV ||
               state->kind == SOCKET_FUTURE_UDP_RECVFROM) {
        state->empty_result = true;
        state->completed = true;
    }
}

static bool socket_future_start(JSContext *ctx,
                                esp32_mquickjs_runtime_t *runtime,
                                esp32_mquickjs_future_token_t token,
                                esp32_mquickjs_future_driver_state_t *state)
{
    socket_entry_t *entry = socket_future_entry(state);
    esp_timer_create_args_t timer_args = {0};

    if (state == NULL || entry == NULL) {
        JS_ThrowReferenceError(ctx, "socket was closed before operation start");
        return false;
    }
    if (entry->busy) {
        JS_ThrowInternalError(ctx, "socket already has an operation in progress");
        return false;
    }
    entry->busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->timeout_ms > 0) {
        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
    }
    socket_future_step(state);
    socket_future_complete_nonblocking(state);
    if (!state->completed) {
        timer_args.callback = socket_future_poll_timer;
        timer_args.arg = state;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "mqjs_socket";
        timer_args.skip_unhandled_events = true;
        if (esp_timer_create(&timer_args, &state->poll_timer) != ESP_OK ||
            esp_timer_start_periodic(state->poll_timer,
                                     SOCKET_POLL_INTERVAL_US) != ESP_OK) {
            socket_future_stop_timer(state);
            JS_ThrowInternalError(ctx, "failed to start socket readiness poller");
            return false;
        }
    }
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t socket_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    socket_future_step(state);
    if (state != NULL && state->completed) {
        socket_future_stop_timer(state);
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue socket_future_make_udp_result(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef result_ref;
    JSGCRef data_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *data_view = JS_PushGCRef(ctx, &data_ref);

    if (!socket_format_address(&state->address,
                               state->host,
                               sizeof(state->host),
                               &state->remote_port)) {
        snprintf(state->host, sizeof(state->host), "unknown");
    }
    *result = JS_NewObject(ctx);
    *data_view = JS_IsException(*result)
        ? JS_EXCEPTION
        : esp32_mquickjs_new_owned_byte_view(ctx,
                                             state->data,
                                             (size_t)state->received);
    if (!JS_IsException(*data_view)) {
        state->data = NULL;
    }
    if (JS_IsException(*result) || JS_IsException(*data_view) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "data", *data_view) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "remoteHost",
                                         JS_NewString(ctx, state->host)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "remotePort",
                                         JS_NewInt32(ctx, state->remote_port))) {
        JS_PopGCRef(ctx, &data_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue socket_future_finish(JSContext *ctx,
                                    esp32_mquickjs_future_driver_state_t *state)
{
    socket_entry_t *entry = socket_future_entry(state);

    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "socket operation cancelled");
    }
    if (state->error_code != 0) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        if (state->tls_error.present) {
            const char *operation =
                state->kind == SOCKET_FUTURE_TCP_CONNECT
                    ? "socket.tcp.connect()"
                    : state->kind == SOCKET_FUTURE_TCP_SEND
                          ? "socket.tcp.send()"
                          : "socket.tcp.recv()";

            return esp32_mquickjs_throw_tls_error(
                ctx, operation, &state->tls_error);
        }
#endif
        return JS_ThrowInternalError(ctx,
                                     "%s%s%s: errno=%d",
                                     state->kind == SOCKET_FUTURE_TCP_CONNECT
                                         ? "socket.tcp.connect()"
                                         : state->kind == SOCKET_FUTURE_TCP_ACCEPT
                                             ? "socket.tcp.accept()"
                                             : state->kind == SOCKET_FUTURE_TCP_SEND
                                                 ? "socket.tcp.send()"
                                                 : state->kind == SOCKET_FUTURE_TCP_RECV
                                                     ? "socket.tcp.recv()"
                                                     : state->kind == SOCKET_FUTURE_UDP_SENDTO
                                                         ? "socket.udp.sendto()"
                                                         : "socket.udp.recvfrom()",
                                     state->error_text[0] != '\0' ? " " : " failed",
                                     state->error_text,
                                     state->error_code);
    }
    if (entry == NULL) {
        return JS_ThrowReferenceError(ctx, "socket was closed during operation");
    }
    switch (state->kind) {
        case SOCKET_FUTURE_TCP_CONNECT:
            entry->connected = true;
            entry->peer_closed = false;
            entry->remote_port = state->port;
            snprintf(entry->remote_host, sizeof(entry->remote_host), "%s", state->host);
            socket_refresh_local_address(entry);
            return JS_NewBool(true);
        case SOCKET_FUTURE_TCP_ACCEPT: {
            socket_entry_t *client;

            if (state->empty_result) {
                return JS_NULL;
            }
            client = socket_allocate_entry();
            if (client == NULL || !socket_set_nonblocking(state->client_fd)) {
                if (client != NULL) {
                    socket_reset_entry(client);
                }
                return JS_ThrowInternalError(ctx, "socket handle limit reached");
            }
            client->fd = state->client_fd;
            state->client_fd = -1;
            client->protocol = SOCKET_PROTOCOL_TCP;
            client->connected = true;
            socket_refresh_local_address(client);
            if (!socket_format_address(&state->address,
                                       client->remote_host,
                                       sizeof(client->remote_host),
                                       &client->remote_port)) {
                snprintf(client->remote_host, sizeof(client->remote_host), "unknown");
            }
            return JS_NewInt32(ctx, client->id);
        }
        case SOCKET_FUTURE_TCP_SEND:
            entry->sent_bytes += (uint32_t)state->offset;
            return JS_NewInt32(ctx, (int32_t)state->offset);
        case SOCKET_FUTURE_TCP_RECV:
            if (state->empty_result) {
                return JS_NULL;
            }
            entry->received_bytes += (uint32_t)state->received;
            {
                JSValue view = esp32_mquickjs_new_owned_byte_view(
                    ctx, state->data, (size_t)state->received);
                if (!JS_IsException(view)) {
                    state->data = NULL;
                }
                return view;
            }
        case SOCKET_FUTURE_UDP_SENDTO:
            entry->sent_bytes += (uint32_t)state->offset;
            return JS_NewInt32(ctx, (int32_t)state->offset);
        case SOCKET_FUTURE_UDP_RECVFROM:
            if (state->empty_result) {
                return JS_NULL;
            }
            entry->received_bytes += (uint32_t)state->received;
            return socket_future_make_udp_result(ctx, state);
    }
    return JS_ThrowInternalError(ctx, "invalid socket Future kind");
}

static bool socket_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    state->completed = true;
    socket_future_stop_timer(state);
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return true;
}

static void socket_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    socket_entry_t *entry;

    if (state == NULL) {
        return;
    }
    socket_future_stop_timer(state);
    entry = socket_future_entry(state);
    if (entry != NULL) {
        entry->busy = false;
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        if (state->kind == SOCKET_FUTURE_TCP_CONNECT &&
            entry->secure && !entry->connected) {
            socket_close_tls_connection(entry);
        }
#endif
    }
    if (state->client_fd >= 0) {
        close(state->client_fd);
    }
    socket_future_release_send_source(state);
    heap_caps_free(state->data);
    heap_caps_free(state);
}

static uint32_t socket_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return 0;
}

#define SOCKET_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .prepare = prepare_fn, \
        .start = socket_future_start, \
        .poll = socket_future_poll, \
        .finish = socket_future_finish, \
        .cancel = socket_future_cancel, \
        .destroy = socket_future_destroy, \
        .timeout_ms = socket_future_timeout_ms, \
    }

SOCKET_FUTURE_DRIVER(s_socket_tcp_connect_driver, socket_tcp_connect_future_prepare);
SOCKET_FUTURE_DRIVER(s_socket_tcp_accept_driver, socket_tcp_accept_future_prepare);
SOCKET_FUTURE_DRIVER(s_socket_tcp_send_driver, socket_tcp_send_future_prepare);
SOCKET_FUTURE_DRIVER(s_socket_tcp_recv_driver, socket_tcp_recv_future_prepare);
SOCKET_FUTURE_DRIVER(s_socket_udp_sendto_driver, socket_udp_sendto_future_prepare);
SOCKET_FUTURE_DRIVER(s_socket_udp_recvfrom_driver, socket_udp_recvfrom_future_prepare);

#undef SOCKET_FUTURE_DRIVER

static bool socket_register_future_drivers(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef socket_ref;
    JSGCRef tcp_ref;
    JSGCRef udp_ref;
    JSGCRef connect_ref;
    JSGCRef accept_ref;
    JSGCRef send_ref;
    JSGCRef recv_ref;
    JSGCRef sendto_ref;
    JSGCRef recvfrom_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *socket = JS_PushGCRef(ctx, &socket_ref);
    JSValue *tcp = JS_PushGCRef(ctx, &tcp_ref);
    JSValue *udp = JS_PushGCRef(ctx, &udp_ref);
    JSValue *connect_fn = JS_PushGCRef(ctx, &connect_ref);
    JSValue *accept_fn = JS_PushGCRef(ctx, &accept_ref);
    JSValue *send_fn = JS_PushGCRef(ctx, &send_ref);
    JSValue *recv_fn = JS_PushGCRef(ctx, &recv_ref);
    JSValue *sendto_fn = JS_PushGCRef(ctx, &sendto_ref);
    JSValue *recvfrom_fn = JS_PushGCRef(ctx, &recvfrom_ref);
    bool result;

    *global = JS_GetGlobalObject(ctx);
    *socket = JS_IsException(*global) ? JS_EXCEPTION
                                      : JS_GetPropertyStr(ctx, *global, "socket");
    *tcp = JS_IsException(*socket) ? JS_EXCEPTION
                                   : JS_GetPropertyStr(ctx, *socket, "tcp");
    *udp = JS_IsException(*socket) ? JS_EXCEPTION
                                   : JS_GetPropertyStr(ctx, *socket, "udp");
    *connect_fn = JS_IsException(*tcp) ? JS_EXCEPTION
                                       : JS_GetPropertyStr(ctx, *tcp, "connect");
    *accept_fn = JS_IsException(*tcp) ? JS_EXCEPTION
                                      : JS_GetPropertyStr(ctx, *tcp, "accept");
    *send_fn = JS_IsException(*tcp) ? JS_EXCEPTION
                                    : JS_GetPropertyStr(ctx, *tcp, "send");
    *recv_fn = JS_IsException(*tcp) ? JS_EXCEPTION
                                    : JS_GetPropertyStr(ctx, *tcp, "recv");
    *sendto_fn = JS_IsException(*udp) ? JS_EXCEPTION
                                      : JS_GetPropertyStr(ctx, *udp, "sendto");
    *recvfrom_fn = JS_IsException(*udp) ? JS_EXCEPTION
                                        : JS_GetPropertyStr(ctx, *udp, "recvfrom");
    result = !JS_IsException(*connect_fn) && !JS_IsException(*accept_fn) &&
             !JS_IsException(*send_fn) && !JS_IsException(*recv_fn) &&
             !JS_IsException(*sendto_fn) && !JS_IsException(*recvfrom_fn) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *connect_fn,
                                                    &s_socket_tcp_connect_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *accept_fn,
                                                    &s_socket_tcp_accept_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *send_fn,
                                                    &s_socket_tcp_send_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *recv_fn,
                                                    &s_socket_tcp_recv_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *sendto_fn,
                                                    &s_socket_udp_sendto_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *recvfrom_fn,
                                                    &s_socket_udp_recvfrom_driver);
    if (!result) {
        JS_ThrowInternalError(ctx, "failed to register socket Future drivers");
    }
    JS_PopGCRef(ctx, &recvfrom_ref);
    JS_PopGCRef(ctx, &sendto_ref);
    JS_PopGCRef(ctx, &recv_ref);
    JS_PopGCRef(ctx, &send_ref);
    JS_PopGCRef(ctx, &accept_ref);
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &udp_ref);
    JS_PopGCRef(ctx, &tcp_ref);
    JS_PopGCRef(ctx, &socket_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_socket_tcp_connect(JSContext *ctx,
                              JSValue *this_val,
                              int argc,
                              JSValue *argv)
{
    (void)this_val;
    return socket_future_call_and_wait(ctx, "tcp", "connect", argc, argv);
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
    if (entry->connected || entry->busy) {
        return JS_ThrowInternalError(ctx, "TCP socket is already active");
    }
    if (entry->secure) {
        return JS_ThrowTypeError(ctx, "TLS socket handles are client-only and cannot listen");
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
    (void)this_val;
    return socket_future_call_and_wait(ctx, "tcp", "accept", argc, argv);
}

JSValue js_socket_tcp_send(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    (void)this_val;
    return socket_future_call_and_wait(ctx, "tcp", "send", argc, argv);
}

JSValue js_socket_tcp_recv(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    (void)this_val;
    return socket_future_call_and_wait(ctx, "tcp", "recv", argc, argv);
}

JSValue js_socket_udp_sendto(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    (void)this_val;
    return socket_future_call_and_wait(ctx, "udp", "sendto", argc, argv);
}

JSValue js_socket_udp_recvfrom(JSContext *ctx,
                               JSValue *this_val,
                               int argc,
                               JSValue *argv)
{
    (void)this_val;
    return socket_future_call_and_wait(ctx, "udp", "recvfrom", argc, argv);
}

#endif
