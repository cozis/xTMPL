#include <stdbool.h>

#define XT_ERRMSG_MAX 256

typedef struct {
    bool  occurred;
    bool truncated;
    long off, row, col;
    char message[XT_ERRMSG_MAX];
} XT_Error;

typedef struct Value Value;

typedef enum {
    VK_ERROR,
    VK_INT,
    VK_FLOAT,
    VK_ARRAY,
} ValueKind;

typedef struct {
    Value *items;
    int    count, 
        capacity;
} ArrayValue;

typedef bool (*FuncValue)(Value*, int, Value*, const char**);

struct Value {
    ValueKind kind;
    union {
        long long  as_int;
        double     as_float;
        ArrayValue as_array;
        FuncValue  as_func;
    };
};

typedef struct Variables Variables;
typedef struct {
    const char *name;
    long         len;
    Value      value;
} Variable;

struct Variables {
    Variables *parent;
    Variable  *list;
};

typedef void (*xt_callback)(const char*, long, void*);

bool xtmpl2(const char *tmpl, long len, Variables *vars, 
            xt_callback callback, void *userp, XT_Error *err);

char *xtmpl(const char *tmpl, long len, Variables *vars,
            long *outlen, XT_Error *err);