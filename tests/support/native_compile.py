"""Compile and execute native fixtures; no test cases or device operations."""
import pathlib
import shutil
import subprocess
import tempfile
from tests.support.wireless_allocator_fixture import allocation_boundary


def compile_run(case, content, cflags=()):
    compiler = shutil.which('cc')
    if compiler is None:
        case.skipTest('C compiler unavailable')
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp)/'fixture.c'
        c.write_text(allocation_boundary(content))
        result = subprocess.run([compiler, '-std=c11', *cflags, str(c), '-o', tmp+'/test'], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stderr)
