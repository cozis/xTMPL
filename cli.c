#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "xtmpl.h"

static char *load_from_stream(FILE *fp, long *out_size, const char **err)
{
    char *data = NULL;
    long  capacity = 1 << 11;
    long  size = 0;

    while(1) {

        capacity *= 2;
        if((long) capacity < 0) {
            
            if(err) 
                *err = "Too big";
            
            free(data);
            return NULL;
        }

        void *temp = realloc(data, capacity);
        if(temp == NULL) {
            if(err)
                *err = "No memory";
            free(data);
            return NULL;
        }
        data = temp;

        long unused = capacity - size - 1; // Spare one byte for NULL termination.
        long num = fread(data + size, 1, unused, fp);
        size += num;

        if(num < unused) {
            // Either something went wrong or
            // we're done copying.
            if(ferror(fp)) {
                if(err)
                    *err = "Unknown read error";
                free(data);
                return NULL;
            }

            break;
        }
    }
    data[size] = '\0';

    if(out_size)
        *out_size = size;

    return data;
}

int main()
{
    const char *err;
    char  *tmpl_str;
    long   tmpl_len;
    tmpl_str = load_from_stream(stdin, &tmpl_len, &err);
    if(tmpl_str == NULL) {
        assert(err != NULL);
        fprintf(stderr, "Error: Failed to read input (%s)\n", err);
        return -1;
    }

    long   len;
    char *str = xtmpl(tmpl_str, tmpl_len, &len, &err);
    if(str == NULL) {
        assert(err != NULL);
        fprintf(stderr, "Error: %s\n", err);
        free(tmpl_str);
        return -1;
    }

    printf("%s", str);

    free(tmpl_str);
    free(str);
    return 0;
}