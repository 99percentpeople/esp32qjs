#include "repl_line.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expect_size(size_t actual, size_t expected, const char *message)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %zu, got %zu)\n", message, expected, actual);
        exit(1);
    }
}

static void test_multiline_detection(void)
{
    expect_true(esp32qjs_repl_needs_multiline_continuation("(1 +", strlen("(1 +")),
                "open parenthesis should continue");
    expect_true(esp32qjs_repl_needs_multiline_continuation("'abc", strlen("'abc")),
                "unterminated single quote should continue");
    expect_true(esp32qjs_repl_needs_multiline_continuation("`abc", strlen("`abc")),
                "unterminated template should continue");
    expect_true(esp32qjs_repl_needs_multiline_continuation("/* block", strlen("/* block")),
                "unterminated block comment should continue");
    expect_true(!esp32qjs_repl_needs_multiline_continuation("(1 +\n2)", strlen("(1 +\n2)")),
                "closed expression should not continue");
    expect_true(!esp32qjs_repl_needs_multiline_continuation("// comment\n1", strlen("// comment\n1")),
                "line comment ending with newline should not continue");
    expect_true(!esp32qjs_repl_needs_multiline_continuation("\"abc\"", strlen("\"abc\"")),
                "closed double quote should not continue");
}

static void test_layout_basic(void)
{
    esp32qjs_repl_layout_t layout = {0};

    esp32qjs_repl_compute_layout("", 0, 0, 10, 4, 4, &layout);
    expect_size(layout.rows, 1, "empty buffer rows");
    expect_size(layout.cursor_row, 0, "empty buffer cursor row");
    expect_size(layout.cursor_col, 4, "empty buffer cursor col");

    esp32qjs_repl_compute_layout("1234567", 7, 7, 10, 4, 4, &layout);
    expect_size(layout.rows, 2, "wrapped buffer rows");
    expect_size(layout.cursor_row, 1, "wrapped buffer cursor row");
    expect_size(layout.cursor_col, 1, "wrapped buffer cursor col");
}

static void test_layout_multiline(void)
{
    esp32qjs_repl_layout_t layout = {0};
    const char *source = "ab\nc";

    esp32qjs_repl_compute_layout(source, strlen(source), strlen(source), 10, 4, 4, &layout);
    expect_size(layout.rows, 2, "newline rows");
    expect_size(layout.cursor_row, 1, "newline cursor row");
    expect_size(layout.cursor_col, 5, "newline cursor col");

    esp32qjs_repl_compute_layout(source, strlen(source), 2, 10, 4, 4, &layout);
    expect_size(layout.cursor_row, 0, "cursor before newline row");
    expect_size(layout.cursor_col, 6, "cursor before newline col");
}

int main(void)
{
    test_multiline_detection();
    test_layout_basic();
    test_layout_multiline();
    return 0;
}
