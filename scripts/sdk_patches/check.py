"""Read-only SDK fingerprint and binary composition preflight; no hardware."""
import hashlib
import subprocess

from sdk_patches.registry import catalog, module, reviewed_inputs


def check_inputs(idf, inputs):
    results = []
    for relative, record in inputs.items():
        path = idf / relative
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        results.append({'path': relative, 'patches': record['patches'],
                        'expected': record['sha256'], 'actual': actual,
                        'status': 'matched' if actual == record['sha256'] else 'changed' if actual else 'missing'})
    return results


def check_composition(idf, target, extended):
    """Exercise the actual archive pipeline, including optional predecessor edits."""
    wifi = idf / 'components/esp_wifi/lib' / target
    net = (wifi / 'libnet80211.a').read_bytes()
    pp = (wifi / 'libpp.a').read_bytes()
    options = {'ftm_report_null_fix': extended, 'nan_sd_buffer_fix': extended and target == 'esp32c5',
               'offchan_frame_fix': extended,
               'twt_probe_buffer_fix': extended and target == 'esp32c5',
               'twt_probe_wake_fix': extended and target == 'esp32c5'}
    net = module('wifi.vendor_ie_context').patch_archive(net, target, **options)
    pp = module('wifi.csi_rx_copy').patch_archive(pp, target)
    net = module('wifi.tx_rate').patch_archive(net, target)
    net = module('wifi.raw_tx_management').patch_archive(net, target)
    identity = module('wifi.raw_tx_identity')
    identity.verify_pp(pp, target)
    net = identity.patch_archive(net, target)
    return {'net80211Sha256': hashlib.sha256(net).hexdigest(),
            'ppSha256': hashlib.sha256(pp).hexdigest()}


def inspect_sdk(idf, targets):
    baseline = catalog()['baseline']
    revision = subprocess.run(['git', '-C', str(idf), 'rev-parse', 'HEAD'],
                              capture_output=True, text=True, check=False)
    actual = revision.stdout.strip() if revision.returncode == 0 else None
    inputs = check_inputs(idf, reviewed_inputs())
    compositions = []
    for target in targets:
        for extended in (False, True):
            row = {'target': target, 'optionalPatches': extended}
            try:
                row.update(check_composition(idf, target, extended))
                row['status'] = 'passed'
            except (OSError, ValueError) as error:
                row.update(status='failed', error=str(error))
            compositions.append(row)
    passed = (actual == baseline['idfCommit']
              and all(row['status'] == 'matched' for row in inputs)
              and all(row['status'] == 'passed' for row in compositions))
    return {'schema': 1, 'status': 'passed' if passed else 'failed',
            'baseline': baseline, 'idfPath': str(idf), 'idfCommit': actual,
            'revisionMatches': actual == baseline['idfCommit'],
            'inputs': inputs, 'compositions': compositions,
            'evidence': 'SDK inputs and archive composition only; source fixtures, target builds and hardware acceptance are separate'}
