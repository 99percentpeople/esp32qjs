"""Harden reviewed SDK SmartConfig ACK and decoder secret release in build copies.

This is the native ACK boundary, not proof of decoder/event retirement. A public
Session also uses Radio ownership, native decoder stop and an event fence.
"""
from __future__ import annotations

import argparse
from sdk_patches.common.source import function, replace
import hashlib
from pathlib import Path
import subprocess
import struct
import tempfile
from sdk_patches.wifi.smartconfig_stack import patch_archive as patch_stack_archive

REVIEWED = {
    'src/smartconfig.c': '2230ae439ad0669d50172d8acfe162d7a7d39390a49650b57b587041926b962d',
    'src/smartconfig_ack.c': '0c73d60329e9c2555d412fd0517e26922eb4590ab29b218642c4dd26c31c8ed3',
    'include/esp_smartconfig.h': 'e93894d1e276bb4d76e6cf423a59487375881a12484094fdc14893040d3c7fd9',
    'include/smartconfig_ack.h': '2a2dba78e5a1f85c279438a0a0fc5dccaf33ccf611880c6cf3d2447e271b246e',
    'CMakeLists.txt': '827b80c66d553fe4855b7e95020b45dec1eef08d546f57d3ee2b83778c375871',
    'lib/esp32c3/libsmartconfig.a': 'f1a914a1a2738b8e9c9887d2cdd117fd9e3ca4ce4b24912f41db7be91913c629',
    'esp32c3/esp_adapter.c': '06973e30a235812f837fe32fd91ee41b32bf889b3813f491746666870e8af45f',
    '../esp_coex/esp32c3/esp_coex_adapter.c': '48bccbf6b585ad7e6634d1bba26f9a83eca32371352aeeb9fbe9bef97d28bd12',
    'lib/esp32s3/libsmartconfig.a': '5b7827fd40df928cc03dcb2dafaa06cb53142715837de3bbb3921b0312b6720d',
    'esp32s3/esp_adapter.c': 'aedf22d0832963eaf206ef2571c46ed515a91f72f7d017c46ec830ce8efdd3a0',
    '../esp_coex/esp32s3/esp_coex_adapter.c': '7fb34f00fb22a158b73252f4be55800b429623153a050998b5e3a1fb015b5c1d',
    'lib/esp32c5/libsmartconfig.a': '56ab6008736a8bf271d1c669e99c31a7e7c9c9b319f3548ae8f6d1123490d90b',
    'esp32c5/esp_adapter.c': 'cd43bb333b2fb7cc38590560b1f1fadf5a1aae1314d740185dc7626d8d1b30cc',
    '../esp_coex/esp32c5/esp_coex_adapter.c': '8a2a75be1e1721d29ee58b6f40c44747928c37fc81b25d7a434b2d7f03c0fa21',
    '../esp_timer/src/ets_timer_legacy.c': 'fb28b0f5b2d15950bde406cd327992678c52300cef39e5f9462259f2fd2d083f',
    'lib/esp32c3/libnet80211.a': '0fcbed322d3254063b2c191bc8b8004e703ffd640296164a6552676180909a0c',
    'lib/esp32s3/libnet80211.a': '265e5c89ac0d2a9444b5b466afee49f631ec549f96774f91a721ab5069c58e20',
    'lib/esp32c5/libnet80211.a': '4c86fc1d2f40a933af7f672972978eb7c82651d838fd10584e006438d585793b',
    '../wpa_supplicant/esp_supplicant/src/esp_wifi_driver.h': 'b11d232c5f41a83ecaa19691e8c081e42530da3cea70a5f87462a8ced7b688cb',
    'include/esp_private/wifi_os_adapter.h': '08fb73f76da7e6800c42dbff2fb724dff5205e1c8262e969da0f391be21f1eae',
    '../heap/include/esp_heap_caps.h': '926eb9eada9d675c0302b34e2ed6c4977a9267963dca39e50550ee90efadad97',
    '../heap/heap_caps.c': '6576a54104902164e726a0a6149e01cbbbe1aa1c5a388125bc77cb587ff911b4',
}


# Fixed archive section offsets, before linking/relaxation. Each removed store
# repeats calloc's zero initialization, but precedes the SDK's NULL check.
# Keep instruction widths, section sizes, branch targets and relocations intact.
# Xtensa encodings are assembled with the actual ESP32-S3 target toolchain.
NULL_STORE_PATCHES = {
    'esp32c3': {
        '.text.sc_init_sniffer_glob': (0x2e, '232c0504', '13000000'),
        '.text.TOUCH_Init_guide_glob': (0x5e, '23260500', '13000000'),
    },
    'esp32c5': {
        '.text.sc_init_sniffer_glob': (0x2e, '232c0504', '13000000'),
        '.text.TOUCH_Init_guide_glob': (0x5e, '23260500', '13000000'),
    },
    'esp32s3': {
        '.text.sc_init_sniffer_glob': (0x37, '326a16', 'f02000'),
        '.text.TOUCH_Init_guide_glob': (0x54, '393a', '3df0'),
    },
}


def patch_decoder_null_stores(target: str, archive: bytes) -> bytes:
    """Remove two reviewed redundant stores in an otherwise unchanged archive."""
    relative = 'lib/' + target + '/libsmartconfig.a'
    if target not in NULL_STORE_PATCHES or hashlib.sha256(archive).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SmartConfig NULL-store input: ' + relative)
    if not archive.startswith(b'!<arch>\n'):
        raise ValueError('Expected a regular SmartConfig archive')
    output = bytearray(archive)
    position = 8
    found = set()
    while position < len(archive):
        header = archive[position:position + 60]
        if len(header) != 60 or header[58:60] != b'`\n':
            raise ValueError('Invalid archive member header')
        length = int(header[48:58])
        base = position + 60
        member = archive[base:base + length]
        if member.startswith(b'\x7fELF'):
            if member[:7] != b'\x7fELF\x01\x01\x01':
                raise ValueError('Expected a little-endian ELF32 member')
            shoff = struct.unpack_from('<I', member, 32)[0]
            shsize, shcount, names_index = struct.unpack_from('<HHH', member, 46)
            headers = [struct.unpack_from('<IIIIIIIIII', member, shoff + i * shsize)
                       for i in range(shcount)]
            names_header = headers[names_index]
            names = member[names_header[4]:names_header[4] + names_header[5]]
            for index, section in enumerate(headers):
                name = names[section[0]:names.index(b'\0', section[0])].decode()
                if name not in NULL_STORE_PATCHES[target]:
                    continue
                if name in found:
                    raise ValueError('Duplicate SmartConfig section: ' + name)
                offset, before, after = NULL_STORE_PATCHES[target][name]
                before, after = bytes.fromhex(before), bytes.fromhex(after)
                if len(before) != len(after) or offset + len(before) > section[5]:
                    raise ValueError('Invalid NULL-store patch span')
                start = section[4] + offset
                if member[start:start + len(before)] != before:
                    raise ValueError('Unexpected SmartConfig NULL-store opcode: ' + name)
                # No linker relocation may later overwrite a patched opcode.
                for relocation in headers:
                    if relocation[1] not in (4, 9) or relocation[7] != index:
                        continue
                    for entry in range(relocation[4], relocation[4] + relocation[5], relocation[9]):
                        relocated = struct.unpack_from('<I', member, entry)[0]
                        if offset <= relocated < offset + len(before):
                            raise ValueError('Relocated SmartConfig NULL-store opcode')
                output[base + start:base + start + len(before)] = after
                found.add(name)
        position = base + length + length % 2
    if found != set(NULL_STORE_PATCHES[target]):
        raise ValueError('Missing SmartConfig NULL-store patch section')
    return bytes(output)


SECURE_FREE = '''/* Only libsmartconfig uses this table. Preserve the allocator and
 * exact pointer; scrub the complete live allocation before its normal free.
 * This includes packed decoder bytes outside the SDK's decoded string fields.
 * No extra allocation, header, registry, or global Wi-Fi free interception. */
extern void esp32_mquickjs_wireless_secure_zero(void *data, size_t length);
extern void *psni_info;
static void esp32qjs_smartconfig_secure_free(void *data)
{
    if (data) {
        /* v2 init frees this owner after its second calloc fails, but the SDK
         * omits NULL assignment there. Revoke before restart can free it again.
         * All SmartConfig OSI frees run on the native decoder task. */
        if (data == psni_info) psni_info = NULL;
        esp32_mquickjs_wireless_secure_zero(data, heap_caps_get_allocated_size(data));
    }
    free(data);
}
'''


OBSERVED_CALLOC = '''extern void esp32_mquickjs_wifi_smartconfig_allocation_failed(void);
static void *esp32qjs_smartconfig_calloc(size_t count, size_t size)
{
    void *data = wifi_calloc(count, size);
    if (!data && count && size) {
        /* Record native control state synchronously. No event queue, allocation
         * or decoder stop from inside its allocator call. */
        esp32_mquickjs_wifi_smartconfig_allocation_failed();
    }
    return data;
}
'''


def patch_adapter(target: str, source: bytes) -> bytes:
    relative = target + '/esp_adapter.c'
    if relative not in REVIEWED or hashlib.sha256(source).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SmartConfig adapter: ' + relative)
    text = source.decode()
    anchor = 'wifi_osi_funcs_t g_wifi_osi_funcs = {'
    if text.count(anchor) != 1:
        raise ValueError('Unexpected Wi-Fi OS adapter table')
    start = text.index(anchor)
    end = text.index('\n};', start) + 3
    table = text[start:end]
    table = replace(table, anchor,
                    'static const wifi_osi_funcs_t s_esp32qjs_smartconfig_osi = {')
    table = replace(table, '._free = free,', '._free = esp32qjs_smartconfig_secure_free,')
    table = replace(table, '._wifi_calloc = wifi_calloc,', '._wifi_calloc = esp32qjs_smartconfig_calloc,')
    # A separate immutable flash table avoids mutating the shared driver's OSI
    # callbacks and avoids retaining another full table in internal SRAM.
    return (text + '\n' + SECURE_FREE + '\n' + OBSERVED_CALLOC + '\n' + table + '\n'
            'const wifi_osi_funcs_t *const esp32qjs_smartconfig_osi = '
            '&s_esp32qjs_smartconfig_osi;\n').encode()


def patch_archive(target: str, source: Path, objcopy: str, output: Path) -> None:
    relative = 'lib/' + target + '/libsmartconfig.a'
    if relative not in REVIEWED or hashlib.sha256(source.read_bytes()).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SmartConfig archive: ' + relative)
    if source.resolve() == output.resolve():
        raise ValueError('Cannot overwrite the SDK archive')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        prepared = Path(temporary) / 'prepared.a'
        prepared.write_bytes(patch_stack_archive(patch_decoder_null_stores(target, source.read_bytes()), target))
        patched = Path(temporary) / 'libsmartconfig.a'
        subprocess.run([objcopy, '--enable-deterministic-archives',
                        '--redefine-sym', 'g_osi_funcs_p=esp32qjs_smartconfig_osi',
                        str(prepared), str(patched)], check=True)
        content = patched.read_bytes()
        if not output.exists() or output.read_bytes() != content:
            output.write_bytes(content)






CONTROL = '''/* ESP32QJS: one exact worker until its final resource access. STOP
 * revokes permission; it does not report that the task/socket has retired. */
static portMUX_TYPE s_sc_ack_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint64_t identity, owner;
    bool busy, stopping, completed;
    esp_err_t error, observation_error;
    int socket_errno;
} s_sc_ack;

extern void esp32_mquickjs_wireless_secure_zero(void *data, size_t length);

/* Keep the ACK lane reserved through decoder shutdown, including the interval
 * after a worker retires. An unrelated default handler cannot reuse it. */
esp_err_t esp32qjs_smartconfig_ack_reserve(uint64_t owner)
{
    if (!owner) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_sc_ack_lock);
    esp_err_t error = s_sc_ack.owner || s_sc_ack.busy ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (error == ESP_OK) s_sc_ack.owner = owner;
    portEXIT_CRITICAL(&s_sc_ack_lock);
    return error;
}

esp_err_t esp32qjs_smartconfig_ack_release(uint64_t owner)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    esp_err_t error = !owner || s_sc_ack.owner != owner || s_sc_ack.busy ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (error == ESP_OK) s_sc_ack.owner = 0;
    portEXIT_CRITICAL(&s_sc_ack_lock);
    return error;
}

esp_err_t esp32qjs_smartconfig_ack_stop(uint64_t owner, uint64_t identity)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    esp_err_t error = !owner || s_sc_ack.owner != owner || !identity ||
        s_sc_ack.identity != identity ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (error == ESP_OK && s_sc_ack.busy) s_sc_ack.stopping = true;
    portEXIT_CRITICAL(&s_sc_ack_lock);
    return error;
}

uint64_t esp32qjs_smartconfig_ack_status(bool *busy, bool *stopping,
    bool *completed, esp_err_t *error, esp_err_t *observation_error, int *socket_errno)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    uint64_t identity = s_sc_ack.identity;
    if (busy) *busy = s_sc_ack.busy;
    if (stopping) *stopping = s_sc_ack.stopping;
    if (completed) *completed = s_sc_ack.completed;
    if (error) *error = s_sc_ack.error;
    if (observation_error) *observation_error = s_sc_ack.observation_error;
    if (socket_errno) *socket_errno = s_sc_ack.socket_errno;
    portEXIT_CRITICAL(&s_sc_ack_lock);
    return identity;
}

static bool sc_ack_allowed(uint64_t identity)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    bool allowed = s_sc_ack.busy && s_sc_ack.identity == identity && !s_sc_ack.stopping;
    portEXIT_CRITICAL(&s_sc_ack_lock);
    return allowed;
}

static void sc_ack_finish(uint64_t identity, esp_err_t error, int socket_errno)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    if (s_sc_ack.busy && s_sc_ack.identity == identity) {
        s_sc_ack.error = error;
        s_sc_ack.socket_errno = socket_errno;
        s_sc_ack.busy = false;
    }
    portEXIT_CRITICAL(&s_sc_ack_lock);
}

static void sc_ack_complete(uint64_t identity)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    bool publish = s_sc_ack.busy && s_sc_ack.identity == identity && !s_sc_ack.stopping;
    if (publish) s_sc_ack.completed = true; /* Control before observation. */
    portEXIT_CRITICAL(&s_sc_ack_lock);
    if (!publish) return;
    esp_err_t error = esp_event_post(SC_EVENT, SC_EVENT_SEND_ACK_DONE, NULL, 0, 0);
    portENTER_CRITICAL(&s_sc_ack_lock);
    if (s_sc_ack.busy && s_sc_ack.identity == identity) s_sc_ack.observation_error = error;
    portEXIT_CRITICAL(&s_sc_ack_lock);
}
'''

START = '''static esp_err_t sc_ack_start_owned(uint64_t owner, smartconfig_type_t type,
    uint8_t token, const uint8_t *cellphone_ip, uint64_t *receipt)
{
    if (!cellphone_ip || (type != SC_TYPE_ESPTOUCH && type != SC_TYPE_AIRKISS && type != SC_TYPE_ESPTOUCH_V2))
        return ESP_ERR_INVALID_ARG;
    sc_ack_t *ack = calloc(1, sizeof(*ack));
    if (!ack) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&s_sc_ack_lock);
    esp_err_t error = s_sc_ack.busy || s_sc_ack.owner != owner ? ESP_ERR_INVALID_STATE :
        s_sc_ack.identity == UINT64_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) {
        ack->identity = ++s_sc_ack.identity;
        if (receipt) *receipt = ack->identity;
        s_sc_ack.busy = true;
        s_sc_ack.stopping = s_sc_ack.completed = false;
        s_sc_ack.error = s_sc_ack.observation_error = ESP_OK;
        s_sc_ack.socket_errno = 0;
    }
    portEXIT_CRITICAL(&s_sc_ack_lock);
    if (error != ESP_OK) { free(ack); return error; }
    ack->type = type;
    ack->ctx.token = token;
    memcpy(ack->ctx.ip, cellphone_ip, 4);
    /* The task may finish before xTaskCreate returns. Do not touch its argument
     * or reset completion after a successful submission. */
    uint64_t identity = ack->identity;
    if (xTaskCreate(sc_ack_send_task, "sc_ack_send_task", SC_ACK_TASK_STACK_SIZE,
            ack, SC_ACK_TASK_PRIORITY, NULL) != pdPASS) {
        esp32_mquickjs_wireless_secure_zero(ack, sizeof(*ack));
        free(ack);
        sc_ack_finish(identity, ESP_ERR_NO_MEM, 0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp32qjs_smartconfig_ack_start(uint64_t owner, smartconfig_type_t type,
    uint8_t token, const uint8_t *cellphone_ip, uint64_t *receipt)
{
    if (!owner || !receipt || *receipt) return ESP_ERR_INVALID_ARG;
    return sc_ack_start_owned(owner, type, token, cellphone_ip, receipt);
}

esp_err_t sc_send_ack_start(smartconfig_type_t type, uint8_t token, uint8_t *cellphone_ip)
{
    return sc_ack_start_owned(0, type, token, cellphone_ip, NULL);
}
'''

STOP = '''void sc_send_ack_stop(void)
{
    portENTER_CRITICAL(&s_sc_ack_lock);
    if (!s_sc_ack.owner && s_sc_ack.busy) s_sc_ack.stopping = true;
    portEXIT_CRITICAL(&s_sc_ack_lock);
}
'''

HANDLER = '''static void handler_got_ssid_passwd(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)arg; (void)base; (void)event_id;
    const smartconfig_event_got_ssid_pswd_t *evt = data;
    if (!evt) return;
    /* No SSID/password stack copies or credential logs. The framework Session
     * will separately own and scrub its credential delivery copy. */
    uint8_t cellphone_ip[4];
    memcpy(cellphone_ip, evt->cellphone_ip, sizeof(cellphone_ip));
    esp_err_t err = sc_send_ack_start(evt->type, evt->token, cellphone_ip);
    if (err != ESP_OK) ESP_LOGE(TAG, "Send smartconfig ACK error: %d", err);
}
'''


def patch_source(relative: str, source: bytes) -> bytes:
    if relative not in ('src/smartconfig.c', 'src/smartconfig_ack.c'):
        raise ValueError('Not a SmartConfig patch input: ' + relative)
    if hashlib.sha256(source).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SmartConfig SDK source: ' + relative)
    text = source.decode()
    if relative == 'src/smartconfig.c':
        return replace(text, function(text, 'handler_got_ssid_passwd'), HANDLER).encode()

    text = replace(text, '#include <string.h>', '#include <string.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <errno.h>')
    text = replace(text, '    smartconfig_type_t type;      /*!< Smartconfig type(ESPTouch or AirKiss) */',
                   '    uint64_t identity;            /* Exact worker, never reused in this boot. */\n'
                   '    smartconfig_type_t type;      /*!< Smartconfig type(ESPTouch or AirKiss) */')
    text = replace(text, '/* Flag to indicate sending smartconfig ACK or not. */\nstatic bool s_sc_ack_send = false;', CONTROL)
    text = replace(text, function(text, 'sc_ack_send_get_errno'), '')
    text = replace(text, '    int err;\n    int ret;',
                   '    esp_err_t error = ESP_OK;\n    int socket_errno = 0;\n    int ret;\n'
                   '    const uint64_t identity = ack->identity;')
    text = replace(text, '    esp_wifi_get_mac(WIFI_IF_STA, ack->ctx.mac);',
                   '    if (!sc_ack_allowed(identity)) goto _end;\n'
                   '    error = esp_wifi_get_mac(WIFI_IF_STA, ack->ctx.mac);\n'
                   '    if (error != ESP_OK) goto _end;')
    text = replace(text, 'while (s_sc_ack_send)', 'while (sc_ack_allowed(identity))', 2)
    text = replace(text, '        ret = esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);',
                   '        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");\n'
                   '        ret = netif ? esp_netif_get_ip_info(netif, &local_ip) : ESP_ERR_INVALID_STATE;')
    # Every socket failure terminates with the original errno. A receive timeout
    # is also a failed ACK, never an ACK success notification.
    text = replace(text, 'goto _end;', 'goto _socket_error;', 9)
    # The two non-socket exits added above must keep their actual error.
    text = replace(text, 'if (!sc_ack_allowed(identity)) goto _socket_error;', 'if (!sc_ack_allowed(identity)) goto _end;')
    text = replace(text, 'if (error != ESP_OK) goto _socket_error;', 'if (error != ESP_OK) goto _end;')
    text = replace(text, '                if (from.sin_addr.s_addr != INADDR_ANY) {',
                   '                if (!sc_ack_allowed(identity)) goto _end;\n'
                   '                if (from.sin_addr.s_addr != INADDR_ANY) {')
    text = replace(text, '                if (ip_addr != INADDR_BROADCAST) {',
                   '                if (!sc_ack_allowed(identity)) goto _end;\n'
                   '                if (ip_addr != INADDR_BROADCAST) {')
    text = replace(text, '                    sendto(send_sock, &ack->ctx, ack_len, 0, (struct sockaddr*) &server_addr, sin_size);',
                   '                    sendlen = sendto(send_sock, &ack->ctx, ack_len, MSG_DONTWAIT, (struct sockaddr*) &server_addr, sin_size);\n'
                   '                    if (sendlen != ack_len) goto _socket_error;')
    text = replace(text, 'sendlen = sendto(send_sock, &ack->ctx, ack_len, 0,',
                   'sendlen = sendto(send_sock, &ack->ctx, ack_len, MSG_DONTWAIT,', 2)
    old = '''                if (sendlen <= 0) {
                    err = sc_ack_send_get_errno(send_sock);
                    ESP_LOGD(TAG, "send failed, errno %d", err);
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }'''
    text = replace(text, old, '                if (sendlen != ack_len) goto _socket_error;')
    text = replace(text, '                    esp_event_post(SC_EVENT, SC_EVENT_SEND_ACK_DONE, NULL, 0, portMAX_DELAY);',
                   '                    sc_ack_complete(identity);')
    text = replace(text, 'sc_ack_complete(identity);\n                    goto _socket_error;',
                   'sc_ack_complete(identity);\n                    goto _end;')
    text = replace(text, '''_end:
    close(send_sock);
    free(ack);
    vTaskDelete(NULL);''', '''    goto _end;
_socket_error:
    socket_errno = errno;
    error = ESP_FAIL;
_end:
    if (send_sock >= 0) close(send_sock);
    esp32_mquickjs_wireless_secure_zero(ack, sizeof(*ack));
    free(ack);
    sc_ack_finish(identity, error, socket_errno); /* Last resource access. */
    vTaskDelete(NULL);''')
    text = replace(text, function(text, 'sc_send_ack_start'), START)
    text = replace(text, function(text, 'sc_send_ack_stop'), STOP)
    return text.encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--target', choices=('esp32c3', 'esp32s3', 'esp32c5'))
    parser.add_argument('--objcopy')
    args = parser.parse_args()
    if bool(args.target) != bool(args.objcopy):
        parser.error('--target and --objcopy must be provided together')
    if args.output_dir.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    for relative, digest in REVIEWED.items():
        if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
            parser.error('Unreviewed SmartConfig dependency: ' + relative)
    for relative in ('src/smartconfig.c', 'src/smartconfig_ack.c'):
        content = patch_source(relative, (args.component / relative).read_bytes())
        output = args.output_dir / Path(relative).name
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.exists() or output.read_bytes() != content:
            output.write_bytes(content)
        print('ESP32QJS SmartConfig native fix: ' + output.name + ' ' + hashlib.sha256(content).hexdigest())
    if args.target:
        content = patch_adapter(args.target, (args.component / args.target / 'esp_adapter.c').read_bytes())
        output = args.output_dir / 'esp_adapter.c'
        if not output.exists() or output.read_bytes() != content:
            output.write_bytes(content)
        patch_archive(args.target, args.component / 'lib' / args.target / 'libsmartconfig.a',
                      args.objcopy, args.output_dir / 'libsmartconfig.a')
        print('ESP32QJS SmartConfig decoder: isolated secure-free OSI table and reviewed local-stack cleanup')


if __name__ == '__main__':
    main()
