"""Keep snippet fixtures at their existing heap boundary after budget admission.

These fixtures exercise native control/ownership, not quota policy. The complete
manager is linked separately by test_memory_wireless and the storage budget
fixtures. Existing explicit managed allocator implementations take precedence.
This module only assembles C text; it never imports or executes a test fixture.
"""
import re
from pathlib import Path


def allocation_boundary(source):
    prefix = 'esp32_mquickjs_memory_'
    if not any(prefix + name in source for name in
               ('wireless_alloc', 'wireless_calloc', 'payload_free',
                'queue_create', 'queue_delete', 'mutex_create', 'mutex_delete',
                'event_group_create', 'event_group_delete')):
        return source

    def defined(name):
        return re.search(r'^\s*#\s*define\s+' + name + r'\b', source, re.M) or re.search(
            r'\b' + name + r'\s*\([^;{}]*\)\s*\{', source)

    internal = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs/internal'
    lines = ['#include <stdlib.h>', '#include "' + str(internal / 'esp32_mquickjs_memory.h') + '"']
    caps = 'MALLOC_CAP_8BIT' if re.search(r'#\s*define\s+MALLOC_CAP_8BIT\b', source) else '0'
    if re.search(r'#\s*define\s+MALLOC_CAP_INTERNAL\b', source):
        caps = '(' + caps + ' | ((policy)==ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL ? MALLOC_CAP_INTERNAL : 0))'
    alloc = 'heap_caps_malloc((size),' + caps + ')' if defined('heap_caps_malloc') else 'malloc((size) ? (size) : 1)'
    calloc = 'heap_caps_calloc((count),(size),' + caps + ')' if defined('heap_caps_calloc') else 'calloc((count),(size))'
    free = 'heap_caps_free' if defined('heap_caps_free') else 'free'
    for name, parameters, body in [
        ('wireless_alloc', '(owner,size,policy,role)', alloc),
        ('wireless_calloc', '(owner,count,size,policy,role)', calloc),
        ('payload_free', '(pointer)', free + '(pointer)'),
        ('queue_create', '(owner,count,size,control)', 'xQueueCreate(count,size)'),
        ('queue_delete', '(pointer)', 'vQueueDelete(pointer)'),
        ('mutex_create', '(owner)', 'xSemaphoreCreateMutex()'),
        ('mutex_delete', '(pointer)', 'vSemaphoreDelete(pointer)'),
        ('event_group_create', '(owner)', 'xEventGroupCreate()'),
        ('event_group_delete', '(pointer)', 'vEventGroupDelete(pointer)'),
    ]:
        if re.search(r'^\s*static\b[^;{}]*\b' + prefix + name +
                     r'\s*\([^;{}]*\)\s*\{', source, re.M):
            # Include the real enum/type declarations first, then give an
            # explicit injected function its own linkage name. The fixture's
            # static boundary must not redeclare the public allocator symbol.
            lines.append('#define ' + prefix + name + ' fixture_memory_' + name)
        elif not defined(prefix + name):
            lines.append('#define ' + prefix + name + parameters + ' ' + body)
    return '\n'.join(lines) + '\n' + source
