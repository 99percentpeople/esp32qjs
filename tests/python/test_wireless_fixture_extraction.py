"""Keep production function extraction exact across ordinary C formatting."""
import unittest

from wireless_vm_fixture import ROOT, extract


class WirelessFixtureExtraction(unittest.TestCase):
    def test_inline_end_excludes_following_function_and_preserves_body(self):
        first = 'static int selected(int n)\n{ if(n) { return 1; } return 0; }'
        source = first + '\nint unrelated(void)\n{\n    return 2;\n}\n'
        self.assertEqual(extract(source, 'selected'), first + '\n')

    def test_literals_and_comments_do_not_end_the_function(self):
        first = r'''static const char *selected(void) {
    /* } */ const char *s = "quoted \\\" } {";
    // } {
    if (s[0] != '}') { return "{"; }
    return s;
}'''
        self.assertEqual(extract(first + '\nint next(void) { return 0; }',
                                 'selected'), first + '\n')

    def test_declaration_is_skipped_and_missing_definition_is_rejected(self):
        source = ('int selected(int n);\nvoid earlier(void) {\n'
                  '    if (selected(1)) { return; }\n}\n'
                  'int selected(int n) { return n; }\n')
        self.assertEqual(extract(source, 'selected'),
                         'int selected(int n) { return n; }\n')
        with self.assertRaises(AssertionError):
            extract('int selected(int n);', 'selected')

    def test_actual_csi_wrapper_does_not_capture_its_neighbour(self):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi/'
                  'esp32_mquickjs_wifi_csi.c').read_text()
        body = extract(source, 'js_wifi_csi_frame_samples')
        self.assertNotIn('js_wifi_csi_frame_copy_samples', body)
        self.assertIn('wifi_csi_frame', body)

    def test_function_mention_inside_comment_is_not_a_definition(self):
        source = '''/* Objects will remain in use until selected() is called.
 * The caller owns teardown.
 */
static void selected(void) {
    release();
}
'''
        self.assertEqual(extract(source, 'selected'),
                         'static void selected(void) {\n    release();\n}\n')
