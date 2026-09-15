"""Build order and upstream inputs for the sole reviewed SDK baseline.

Python transformations own their input tables. CMake-only ABI gates read
cmake_inputs.json through the generated build include; the checker uses the
same tables. Derived object fingerprints stay with their transformations.
"""
import importlib
import json
import posixpath
import re
from pathlib import Path

from tool_paths import ROOT

PACKAGE = Path(__file__).resolve().parent


def catalog() -> dict:
    data = json.loads((PACKAGE / 'catalog.json').read_text())
    if data.get('schema') != 1:
        raise ValueError('Expected SDK patch catalog schema 1')
    seen = set()
    for patch in data['patches']:
        if patch['id'] in seen:
            raise ValueError('Duplicate SDK patch: ' + patch['id'])
        if not set(patch['after']) <= seen:
            raise ValueError('SDK patch dependencies must precede ' + patch['id'])
        if not (ROOT / 'cmake/sdk_patches' / patch['cmake']).is_file():
            raise ValueError('Missing CMake adapter: ' + patch['cmake'])
        seen.add(patch['id'])
    return data


def module(name: str):
    return importlib.import_module('sdk_patches.' + name)


def cmake_inputs() -> dict:
    data = json.loads((PACKAGE / 'cmake_inputs.json').read_text())
    if data.get('schema') != 1:
        raise ValueError('Expected CMake SDK inputs schema 1')
    return data


def reviewed_inputs() -> dict[str, dict]:
    """Merge exact upstream hashes, retaining every consuming adapter."""
    result = {}

    def add(owner, base, values):
        for relative, digest in values.items():
            path = posixpath.normpath(posixpath.join(base, relative))
            if path.startswith(('../', '/')) or not re.fullmatch('[0-9a-f]{64}', digest):
                raise ValueError(f'Invalid reviewed SDK input: {owner}/{relative}')
            entry = result.setdefault(path, {'sha256': digest, 'patches': []})
            if entry['sha256'] != digest:
                raise ValueError('Conflicting SDK input fingerprints: ' + path)
            if owner not in entry['patches']:
                entry['patches'].append(owner)

    source_groups = (
        ('rrm', 'wpa.rrm', 'wpa_supplicant', ('REVIEWED',)),
        ('dpp', 'wpa.dpp', 'wpa_supplicant', ('REVIEWED',)),
        ('eap_control', 'wpa.eap.control', 'wpa_supplicant',
         ('REVIEWED', 'EXTRA_REVIEWED', 'CONTROL_REVIEWED')),
        ('wps_native', 'wpa.wps.native', 'wpa_supplicant', ('REVIEWED',)),
        ('smartconfig', 'wifi.smartconfig', 'esp_wifi', ('REVIEWED',)),
        ('nan', 'wifi.nan', 'esp_wifi', ('REVIEWED',)),
        ('nan_usd', 'wpa.nan_usd', 'wpa_supplicant', ('REVIEWED',)),
        ('nan_pairing', 'wifi.nan_pairing', '', ('INPUTS',)),
    )
    for owner, name, component, attributes in source_groups:
        implementation = module(name)
        for attribute in attributes:
            add(owner, 'components/' + component, getattr(implementation, attribute))
    add('netif_timer', 'components/esp_netif', {
        'lwip/esp_netif_lwip.c': module('netif.timer').REVIEWED_SHA256})
    add('eloop', 'components/wpa_supplicant', {'port/eloop.c': module('wpa.eloop').REVIEWED})
    antenna = module('phy.antenna')
    add('phy_antenna', 'components/esp_phy', {
        'src/phy_common.c': antenna.SOURCE_HASH, 'src/phy_init.c': antenna.INIT_HASH})
    for owner, name, attribute, archive in (
        ('vendor_ie_context', 'wifi.vendor_ie_context', 'REVIEWED', 'libnet80211.a'),
        ('csi_rx_copy', 'wifi.csi_rx_copy', 'ARCHIVES', 'libpp.a'),
        ('ap_prestart', 'wifi.ap_prestart', 'ARCHIVES', 'libnet80211.a'),
    ):
        add(owner, 'components/esp_wifi/lib', {
            target + '/' + archive: digest
            for target, digest in getattr(module(name), attribute).items()})
    owners = {patch['cmake']: patch['id'] for patch in catalog()['patches']}
    for filename, inputs in cmake_inputs()['groups'].items():
        add(owners[filename], '', inputs)
    return dict(sorted(result.items()))


def render_cmake() -> str:
    inputs = reviewed_inputs()
    lines = ['# Generated from the SDK patch registry. Do not edit.']
    for variable, reference in cmake_inputs()['variables'].items():
        lines.append(f'set({variable} "{inputs[reference["path"]]["sha256"]}")')
    for patch in catalog()['patches']:
        if patch['apply']:
            lines.append(f'include("${{CMAKE_SOURCE_DIR}}/cmake/sdk_patches/{patch["cmake"]}")')
    return '\n'.join(lines) + '\n'
