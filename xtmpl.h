#include <stdbool.h>

typedef void (*xt_callback)(const char*, long, void*);

bool xtmpl2(const char *tmpl, long len, 
            xt_callback callback, void *userp, 
            const char **err);

char *xtmpl(const char *tmpl, long len, 
            long *outlen, const char **err);