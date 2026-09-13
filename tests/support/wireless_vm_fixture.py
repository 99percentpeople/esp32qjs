"""Real MQuickJS + production ByteView/Source, with native/API fault boundaries."""
from tests.support.fixtures import fixture_text
from tests.support.c_source import extract
import pathlib
import subprocess
import sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from check_js_syntax import compiler_command,VENDOR_DIR
CORE=ROOT/'components/esp32_mquickjs/src/core'
INTERNAL=ROOT/'components/esp32_mquickjs/internal'

def run(command):
    result=subprocess.run(command,capture_output=True,text=True,timeout=60)
    if result.returncode:raise AssertionError(result.stderr)
    return result.stdout



INJECTION=fixture_text('shared/wireless_vm_fixture/injection.inc')

RESET_MACROS='\n'.join('#undef '+n for n in ['JS_NewUint32','JS_NewFloat64','JS_NewObjectClassUser','JS_NewObject','JS_NewArray','JS_NewString','JS_NewStringLen','JS_NewInt64','JS_SetPropertyStr','JS_SetPropertyUint32','JS_PushGCRef','JS_PopGCRef','JS_AddGCRef','JS_DeleteGCRef'])+'\n'

def build(directory,extra,main,classes_extra="",globals_extra="",declarations=""):
    directory=pathlib.Path(directory);directory.mkdir(parents=True,exist_ok=True)
    # Use the production class definitions, methods and finalizers. User class ids
    # retain the production offsets; only the selected Host surface is linked.
    framework=(CORE/'mqjs_stdlib_esp32.c').read_text()
    a=framework.index('static const JSPropDef js_byte_view_proto[]')
    b=framework.index('static const JSPropDef js_display_font_proto[]',a)
    classes=framework[a:b].replace('#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP','')+classes_extra
    generic=(VENDOR_DIR/'mqjs_stdlib.c').read_text()
    point=generic.index('static const JSPropDef js_global_object[]')
    generic=generic[:point]+classes+'\n'+generic[point:]
    generic=generic.replace('static const JSPropDef js_global_object[] = {','static const JSPropDef js_global_object[] = {'+globals_extra+'''
    JS_PROP_CLASS_DEF("ByteView", &js_byte_view_class),
    JS_PROP_CLASS_DEF("ByteSpanSource", &js_byte_span_source_class),
    JS_PROP_CLASS_DEF("BitmapSpanSource", &js_bitmap_span_source_class),''')
    (directory/'stdlib.c').write_text(generic)
    cc=compiler_command();includes=[f'-I{directory}',f'-I{VENDOR_DIR}',f'-I{INTERNAL}',f'-I{ROOT / "components/esp32_mquickjs/include"}']
    generator=directory/'stdlib'
    run([*cc,'-O1','-D_GNU_SOURCE',*includes,str(directory/'stdlib.c'),str(VENDOR_DIR/'mquickjs_build.c'),'-o',str(generator)])
    (directory/'mquickjs_atom.h').write_text(run([str(generator),'-a']))
    (directory/'mqjs_stdlib.h').write_text(run([str(generator)]))
    (directory/'esp32_mquickjs_types.h').write_text('#pragma once\n#include "mquickjs.h"\n')
    (directory/'esp32_mquickjs_memory.h').write_text('#pragma once\n')
    (directory/'esp_heap_caps.h').write_text('#pragma once\n#define MALLOC_CAP_8BIT 1\n')
    prefix=(ROOT/'scripts/mquickjs_syntax_check.c').read_text().split('#define CHECKER_HEAP_SIZE')[0]
    prefix=prefix.replace('#include "mqjs_stdlib.h"',fixture_text('shared/wireless_vm_fixture/build-prefix.inc'))
    helper=extract((CORE/'esp32_mquickjs.c').read_text(),'esp32_mquickjs_set_property_ref')
    prefix=prefix.replace('#include "mqjs_stdlib.h"',declarations+'\n#include "mqjs_stdlib.h"')
    source=prefix+INJECTION+(CORE/'esp32_mquickjs_byte_span.c').read_text()+(CORE/'esp32_mquickjs_byte_source.c').read_text()+helper+extra+RESET_MACROS+main
    (directory/'test.c').write_text(source)
    binary=directory/'test'
    run([*cc,'-O1','-g','-D_GNU_SOURCE',*includes,str(directory/'test.c'),*(str(VENDOR_DIR/n) for n in ['mquickjs.c','dtoa.c','libm.c','cutils.c']),'-lm','-o',str(binary)])
    return binary
