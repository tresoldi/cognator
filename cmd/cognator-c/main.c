#include "cognator.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *stream) {
    fprintf(stream, "usage: cognator <command>\n\ncommands:\n  version\n  help\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argc == 2 ? stdout : stderr);
        return argc == 2 ? 0 : 2;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("cognator %s\n", cg_version_string());
        return 0;
    }
    fprintf(stderr, "cognator: unknown command: %s\n", argv[1]);
    usage(stderr);
    return 2;
}
