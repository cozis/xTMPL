#include <string.h>
#include <stdio.h>
#include "xexpr.h"

int main(int argc, char **argv)
{
    if(argc < 2)
        return -1;

    const char *err;
    Value v = eval(argv[1], strlen(argv[1]), &err);
    if(v.kind == VK_ERROR) {
        fprintf(stderr, "Error: %s\n", err);
        return -1;
    }

    fprintf(stdout, "OK: ");
    print(stdout, v);
    fprintf(stdout, "\n");
    return 0;
}