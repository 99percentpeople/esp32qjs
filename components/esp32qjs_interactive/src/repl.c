#include "repl.h"

#include <stdio.h>

#include "repl_input.h"

static void print_banner_line(const char *line)
{
    if (line != NULL && line[0] != '\0') {
        printf("%s\n", line);
    }
}

void esp32qjs_repl_print_banner(const esp32qjs_interactive_banner_t *banner)
{
    printf("\n");
    if (banner == NULL) {
        return;
    }

    print_banner_line(banner->title);
    print_banner_line(banner->subtitle);
    print_banner_line(banner->hint);
}

bool esp32qjs_repl_process_input(const esp32qjs_interactive_host_t *host)
{
    char line[ESP32QJS_REPL_LINE_SIZE];

    if (!esp32qjs_repl_read_line(line, sizeof(line))) {
        return false;
    }

    if (line[0] == '\0') {
        esp32qjs_repl_print_prompt();
        return true;
    }

    esp32qjs_repl_history_push(line);

    if (host != NULL && host->handle_line != NULL) {
        host->handle_line(host->opaque, line);
    }

    esp32qjs_repl_print_prompt();
    return true;
}
