"""New fs metadata/watch bindings in the vendored VM, with SDK/queue boundaries."""
import tempfile
import unittest

from tests.support.fixtures import fixture_text
from tests.support.wireless_vm_fixture import ROOT, build, extract, run


class FilesystemApiTests(unittest.TestCase):
    def test_mount_metadata_and_watch_arguments_with_moving_gc(self):
        source = (ROOT / "components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs.c").read_text()
        options = (ROOT / "components/esp32_mquickjs/src/core/esp32_mquickjs_options.c").read_text()
        stdlib = (ROOT / "components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c").read_text()
        start = stdlib.index("static const JSPropDef js_fs[]")
        end = stdlib.index("static const JSPropDef js_framework[]", start)
        # Link only the four changed bindings; other fs methods have separate drivers.
        table = "\n".join(line for line in stdlib[start:end].splitlines()
                          if "JS_CFUNC_DEF" not in line or any('"' + name + '"' in line
                              for name in ("mounts", "info", "watch")))
        code = fixture_text("fs/test_fs_api/boundaries.inc")
        code += (ROOT / "components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs_path.c").read_text()
        for name in ("esp32_mquickjs_own_property_keys", "esp32_mquickjs_validate_plain_options",
                     "esp32_mquickjs_value_to_bounded_u32"):
            code += extract(options, name)
        for name in ("fs_runtime", "fs_change_kind_name", "fs_change_to_js", "fs_change_queue_closed",
                     "find_littlefs_mount_by_root", "js_value_to_fs_path", "js_fs_get_root",
                     "fs_make_mount_object", "js_fs_mounts", "js_fs_info", "fs_parse_watch_options",
                     "js_fs_watch"):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as directory:
            declarations = "\n".join(extract(source, name).split("{", 1)[0] + ";"
                                     for name in ("js_fs_get_root", "js_fs_mounts", "js_fs_info", "js_fs_watch"))
            binary = build(directory, code, fixture_text("fs/test_fs_api/main.inc"),
                           classes_extra=table,
                           globals_extra='JS_PROP_CLASS_DEF("fs", &js_fs_obj),',
                           declarations=declarations)
            for moving_gc in ("0", "1"):
                run([str(binary), moving_gc])
