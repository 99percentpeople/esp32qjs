"""Generate camera 2.1.7 fixes in the build directory; never edit managed sources."""
import argparse
import hashlib
import json
from pathlib import Path

from sdk_patches.camera import fragments
from sdk_patches.common.io import write_if_changed
from sdk_patches.common.source import replace

INPUTS = json.loads(Path(__file__).with_name('inputs.json').read_text())


def prepare(component: Path, output: Path) -> None:
    if output.resolve().is_relative_to(component.resolve()):
        raise ValueError('Camera output must be outside the managed component')
    sources = {}
    for name, expected in INPUTS['inputs'].items():
        sources[name] = (component / name).read_bytes()
        if hashlib.sha256(sources[name]).hexdigest() != expected:
            raise ValueError('Unreviewed esp32-camera 2.1.7 input: ' + name)
    hal = replace(sources['driver/cam_hal.c'].decode(), fragments.PATCH_BEFORE, fragments.PATCH_AFTER)
    driver = sources['driver/esp_camera.c'].decode()
    for name in ('PROBE', 'SENSOR_TABLE_START', 'SENSOR_TABLE_END', 'PROBE_GUARD'):
        driver = replace(driver, getattr(fragments, name + '_BEFORE'), getattr(fragments, name + '_AFTER'))
    outputs = {'driver/cam_hal.c': hal, 'driver/esp_camera.c': driver}
    # Keep the complete generated source equivalent to the reviewed corrections,
    # including espressif/esp32-camera#853 and zero-sensor build guards.
    for name, text in outputs.items():
        if hashlib.sha256(text.encode()).hexdigest() != INPUTS['outputs'][name]:
            raise ValueError('Unexpected camera transformation: ' + name)
    for name, text in outputs.items():
        write_if_changed(output / name, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.component, args.output_dir)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print('esp32-camera 2.1.7: reviewed build-local sources')
