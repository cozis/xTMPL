#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include "xtmpl.h"

#define MAX_DEPTH 8

typedef enum {
    VK_ERROR,
    VK_INT,
    VK_FLOAT
} ValueKind;

typedef struct {
    ValueKind kind;
    union {
        long long as_int;
        double    as_float;
    };
} Value;

typedef enum {
    OID_ADD,
    OID_SUB,
    OID_MUL,
    OID_DIV,
} OperatID;

typedef struct {
    const char *err, *str;
    long   prev_i, i, len;
} EvalContext;

static Value eval(const char *str, long len, const char **err);

static bool print(const char *expr, long len, xt_callback callback, 
                  void *userp, const char **err)
{
    Value v = eval(expr, len, err);
    if(v.kind == VK_ERROR)
        return 0;

    char buff[32];
    switch(v.kind) {
        case VK_ERROR:
        /* UNREACHABLE */
        assert(0);
        break;

        case VK_INT:
        {
            int len = snprintf(buff, sizeof(buff), 
                               "%lld", v.as_int);
            assert(len >= 0);
            callback(buff, len, userp);
            break;
        }

        case VK_FLOAT:
        {
            int len = snprintf(buff, sizeof(buff), 
                               "%lf", v.as_float);
            assert(len >= 0);
            callback(buff, len, userp);
            break;
        }
    }
    return 1;
}

static Value apply(OperatID operat, Value lhs, Value rhs)
{
    if(lhs.kind == VK_ERROR)
        return lhs;

    if(rhs.kind == VK_ERROR)
        return rhs;

    switch(operat) {
        case OID_ADD:
        {
            // TODO: Check overflow.

            if(lhs.kind == VK_INT && rhs.kind == VK_INT)
                return (Value) { VK_INT, 
                    .as_int = lhs.as_int 
                            + rhs.as_int };

            if(lhs.kind == VK_INT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_int 
                              + rhs.as_float };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_INT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              + rhs.as_int };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              + rhs.as_float };

            return (Value) { VK_ERROR };
        }

        case OID_SUB:
        {
            // TODO: Check overflow.

            if(lhs.kind == VK_INT && rhs.kind == VK_INT)
                return (Value) { VK_INT, 
                    .as_int = lhs.as_int 
                            - rhs.as_int };

            if(lhs.kind == VK_INT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_int 
                              - rhs.as_float };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_INT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              - rhs.as_int };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              - rhs.as_float };

            return (Value) { VK_ERROR };
        }

        case OID_MUL:
        {
            // TODO: Check overflow.

            if(lhs.kind == VK_INT && rhs.kind == VK_INT)
                return (Value) { VK_INT, 
                    .as_int = lhs.as_int 
                            * rhs.as_int };

            if(lhs.kind == VK_INT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_int 
                              * rhs.as_float };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_INT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              * rhs.as_int };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              * rhs.as_float };

            return (Value) { VK_ERROR };
        }

        case OID_DIV:
        {
            // TODO: Check overflow.

            if(lhs.kind == VK_INT && rhs.kind == VK_INT)
                return (Value) { VK_INT, 
                    .as_int = lhs.as_int 
                            / rhs.as_int };

            if(lhs.kind == VK_INT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_int 
                              / rhs.as_float };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_INT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              / rhs.as_int };

            if(lhs.kind == VK_FLOAT && rhs.kind == VK_FLOAT)
                return (Value) { VK_FLOAT, 
                    .as_float = lhs.as_float 
                              / rhs.as_float };

            return (Value) { VK_ERROR };
        }
    }
    return (Value) { VK_ERROR };
}

static Value eval_inner(EvalContext *ctx);

static Value eval_primary(EvalContext *ctx)
{
    if(ctx->i == ctx->len) {
        ctx->err = "Expression ended where a primary "
                   "expression was expected";
        return (Value) { VK_ERROR };
    }

    if(isdigit(ctx->str[ctx->i])) {

        long long buff = 0;
        do {
            // TODO: Check overflow
            char u = ctx->str[ctx->i] - '0';
            
            if(buff > (LLONG_MAX - u) / 10) {
                ctx->err = "Overflow";
                return (Value) {VK_ERROR};
            }

            buff = buff * 10 + u;
            ctx->i += 1;
        } while(ctx->i < ctx->len && isdigit(ctx->str[ctx->i]));

        if(ctx->i+1 < ctx->len 
            && ctx->str[ctx->i] == '.' 
            && isdigit(ctx->str[ctx->i+1])) {
            
            ctx->i += 1;

            double decimal = 0;
            double q = 1;

            do {
                q /= 10;
                decimal += q * (ctx->str[ctx->i] - '0');
                ctx->i += 1;
            } while(ctx->i < ctx->len && isdigit(ctx->str[ctx->i]));
            
            return (Value) {VK_FLOAT, .as_float = (double) buff + decimal};
        }

        return (Value) {VK_INT, .as_int = buff};
    }
    
    ctx->err = "Unexpected character where a primary "
               "expression was expected";
    return (Value) { VK_ERROR };
}

static bool next_binary_operat(EvalContext *ctx, OperatID *operat)
{
    if(ctx->i < ctx->len) 
        switch(ctx->str[ctx->i]) {
            case '+':
            ctx->i += 1;
            *operat = OID_ADD;
            return 1;
            
            case '-':
            ctx->i += 1;
            *operat = OID_SUB;
            return 1;

            case '*':
            ctx->i += 1;
            *operat = OID_MUL;
            return 1;

            case '/':
            ctx->i += 1;
            *operat = OID_DIV;
            return 1;
        }
    return 0;
}

static long preced_of(OperatID operat)
{
    static const long map[] = {
        [OID_ADD] = 0,
        [OID_SUB] = 0,
        [OID_MUL] = 1,
        [OID_DIV] = 1,
    };
    return map[operat];
}

static bool is_right_assoc(OperatID operat)
{
    (void) operat;
    return 0;
}

static Value eval_expr_1(EvalContext *ctx, Value lhs, long min_preced)
{
    if(lhs.kind == VK_ERROR)
        return lhs;

    OperatID operat;
    int prev_i = ctx->i;
    while(next_binary_operat(ctx, &operat) && preced_of(operat) >= min_preced) {

        Value rhs = eval_primary(ctx);
        if(rhs.kind == VK_ERROR)
            return rhs;

        OperatID operat2;
        int prev_i_2 = ctx->i;
        while(next_binary_operat(ctx, &operat2) && (preced_of(operat2) > preced_of(operat) || (is_right_assoc(operat2) && preced_of(operat) == preced_of(operat2)))) {

            ctx->i = prev_i_2;

            long preced = preced_of(operat) 
                        + (preced_of(operat2) > preced_of(operat));

            rhs = eval_expr_1(ctx, rhs, preced);
            if(rhs.kind == VK_ERROR)
                return rhs;

            prev_i_2 = ctx->i;
        }
        ctx->i = prev_i_2;
/*
        {
            fprintf(stdout, "evaluating: ");
            print(stdout, lhs);
            fprintf(stdout, " %s ", operat_text(operat));
            print(stdout, rhs);
            fprintf(stdout, " = ");
        }
*/
        lhs = apply(operat, lhs, rhs);
/*      
        {
            print(stdout, lhs);
            fprintf(stdout, "\n");
        }
*/
        if(lhs.kind == VK_ERROR)
            return lhs;

        prev_i = ctx->i;
    }
    ctx->i = prev_i;
    return lhs;
}

static Value eval_inner(EvalContext *ctx)
{
    return eval_expr_1(ctx, eval_primary(ctx), 0);
}

static Value eval(const char *str, long len, const char **err)
{
    EvalContext ctx = {
        .err = NULL,
        .str = str,
        .len = len,
          .i = 0,
    };
    Value val = eval_inner(&ctx);
    if(val.kind == VK_ERROR)
        if(err) *err = ctx.err;
    return val;
}

typedef enum {
    SEG_TEXT,
    SEG_EXPR,
    SEG_IF,
    SEG_FOR,
    SEG_ELSE,
    SEG_ENDIF,
    SEG_ENDFOR,
    SEG_END,
} Kind;

typedef struct {
    Kind kind;
    long off, len;
} Segment;

static Segment *tokenize(const char *tmpl, long len, const char **err)
{
    #define SKIP_SPACES()                \
        while(i < len && (tmpl[i] == ' ' \
            || tmpl[i] == '\t'           \
            || tmpl[i] == '\n'))         \
            i += 1;

    Segment *arr = NULL;
    long capacity = 32;
    long count = 0;

    Kind context_stack[MAX_DEPTH];
    int depth = 0;

    long i = 0;
    do {
        Segment text;
        text.kind = SEG_TEXT;
        text.off = i;
        while(i < len && (i+1 > len || tmpl[i] != '{' || (tmpl[i+1] != '%' && tmpl[i+1] != '{')))
            i += 1;
        text.len = i - text.off;
        
        if(text.len > 0) {
            if(arr == NULL || count == capacity) {
                capacity *= 2;
                void *temp = realloc(arr, capacity*sizeof(Segment));
                if(temp == NULL) {
                    free(arr);
                    *err = "Out of memory";
                    return NULL;
                }
                arr = temp;
            }
            arr[count++] = text;
        }

        Segment seg;
        if(i == len) {
            seg.kind = SEG_END;
            seg.off = i;
            seg.len = 0;
        } else if(tmpl[i+1] == '%') {

            assert(tmpl[i] == '{' && tmpl[i+1] == '%');
            i += 2;

            SKIP_SPACES()
            
            if(i == len || (!isalpha(tmpl[i]) && tmpl[i] != '_')) {
                free(arr);
                *err = "block {% .. %} doesn't start with a keyword";
                return NULL;
            }

            long kword_off = i;
            do
                i += 1;
            while(i < len && (isalpha(tmpl[i]) || tmpl[i] == '_'));
            long kword_len = i - kword_off;
            
            SKIP_SPACES()

            switch(kword_len) {
                case 2:
                if(strncmp(tmpl + kword_off, "if", kword_len))
                    goto badkword;
                seg.kind = SEG_IF;
                
                if(depth == MAX_DEPTH) {
                    free(arr);
                    *err = "Too many nested if-else and for blocks";
                    return NULL;
                }
                context_stack[depth++] = SEG_IF;
                break;

                case 3:
                if(strncmp(tmpl + kword_off, "for", kword_len))
                    goto badkword;
                seg.kind = SEG_FOR;

                if(depth == MAX_DEPTH) {
                    free(arr);
                    *err = "Too many nested if-else and for blocks";
                    return NULL;
                }
                context_stack[depth++] = SEG_FOR;
                break;

                case 4:
                if(strncmp(tmpl + kword_off, "else", kword_len))
                    goto badkword;
                seg.kind = SEG_ELSE;

                if(depth == 0 || context_stack[depth-1] != SEG_IF) {
                    free(arr);
                    *err = "{% else %} has no matching {% if .. %}";
                    return NULL;
                }
                break;

                case 5:
                if(strncmp(tmpl + kword_off, "endif", kword_len))
                    goto badkword;
                seg.kind = SEG_ENDIF;

                if(depth == 0 || context_stack[depth-1] != SEG_IF) {
                    free(arr);
                    *err = "{% endif %} has no matching {% if .. %}";
                    return NULL;
                }
                depth -= 1;
                break;

                case 6:
                if(strncmp(tmpl + kword_off, "endfor", kword_len))
                    goto badkword;
                seg.kind = SEG_ENDFOR;

                if(depth == 0 || context_stack[depth-1] != SEG_FOR) {
                    free(arr);
                    *err = "{% endfor %} has no matching {% for .. %}";
                    return NULL;
                }
                depth -= 1;
                break;

                default:
            badkword:
                *err = "Bad {% .. %} block keyword";
                return NULL;
            }

            seg.off = i;
            while(i < len 
                && (i+1 > len 
                || tmpl[i] != '%' 
                || tmpl[i+1] != '}'))
                i += 1;
            seg.len = i - seg.off;

            if(i == len)
                break;

            assert(tmpl[i] == '%' && tmpl[i+1] == '}');
            i += 2;
        
        } else {

            assert(tmpl[i] == '{' && tmpl[i+1] == '{');
            i += 2;

            SKIP_SPACES()

            seg.kind = SEG_EXPR;
            seg.off = i;
            while(i < len 
                && (i+1 > len 
                || tmpl[i] != '}' 
                || tmpl[i+1] != '}'))
                i += 1;
            seg.len = i - seg.off;
            
            if(i < len) {
                assert(tmpl[i]   == '}');
                assert(tmpl[i+1] == '}');
                i += 2;
            }
        }

        if(arr == NULL || count == capacity) {
            capacity *= 2;
            void *temp = realloc(arr, capacity*sizeof(Segment));
            if(temp == NULL) {
                free(arr);
                *err = "Out of memory";
                return NULL;
            }
            arr = temp;
        }
        arr[count++] = seg;

    } while(arr[count-1].kind != SEG_END);

    return arr;
}

typedef struct {
    const char  *err;
    const char *tmpl;
    long      i, len;
    Segment    *segs;
    void      *userp;
    xt_callback callback;
} RenderContext;

static void skip_until_or_end(RenderContext *ctx, Kind until)
{
    int depth = 0;
    while(1) {
        Segment seg = ctx->segs[ctx->i];
        ctx->i += 1;

        if(seg.kind == SEG_END) {
            ctx->i -= 1;
            break;
        }

        if(depth == 0 && seg.kind == until)
            break;

        switch(seg.kind) {
            case SEG_IF:
            case SEG_FOR: 
            depth += 1; 
            break;
            
            case SEG_ENDIF:
            case SEG_ENDFOR:
            depth -= 1;
            break;

            default:
            break;
        }
    }
}

static bool render(RenderContext *ctx, Kind until)
{
    while(ctx->segs[ctx->i].kind != until && ctx->segs[ctx->i].kind != SEG_END) {

        Segment seg = ctx->segs[ctx->i];
        ctx->i += 1;

        switch(seg.kind) {
            
            case SEG_TEXT:
            ctx->callback(ctx->tmpl + seg.off, seg.len, ctx->userp);
            break;

            case SEG_EXPR:
            if(!print(ctx->tmpl + seg.off, seg.len, ctx->callback, ctx->userp, &ctx->err))
                return 0;
            break;

            case SEG_IF:
            {
                const char *expr_str = ctx->tmpl + seg.off;
                long        expr_len = seg.len;

                Value r = eval(expr_str, expr_len, &ctx->err);
                if(r.kind == VK_ERROR) {
                    assert(ctx->err != NULL);
                    return 0;
                }

                bool returned_0 = (r.kind == VK_INT && r.as_int == 0);

                if(!returned_0) {
                    
                    /* -- Took the IF branch -- */
                    
                    // Execute until the {% else %}
                    if(!render(ctx, SEG_ELSE))
                        return 0;

                    // Now skip to the token after the {% endif %}
                    skip_until_or_end(ctx, SEG_ENDIF);

                } else {

                    /* -- Took the ELSE branch -- */

                    // Now skip to the token after the {% else %}
                    skip_until_or_end(ctx, SEG_ELSE);

                    if(!render(ctx, SEG_ENDIF))
                        return 0;
                }
                break;
            }

            case SEG_FOR:
            {
                long start = ctx->i;
                while(1) {

                    const char *expr_str = ctx->tmpl + seg.off;
                    long        expr_len = seg.len;

                    Value r = eval(expr_str, expr_len, &ctx->err);
                    if(r.kind == VK_ERROR) {
                        assert(ctx->err != NULL);
                        return 0;
                    }

                    bool returned_0 = (r.kind == VK_INT && r.as_int == 0);

                    if(returned_0)
                        break;

                    ctx->i = start;
                    if(!render(ctx, SEG_ENDFOR))
                        return 0;
                }
                break;
            }

            case SEG_ELSE:
            ctx->err = "Orphan {% else %}";
            return 0;

            case SEG_END:
            case SEG_ENDIF:
            case SEG_ENDFOR:
            /* Unreachable */
            assert(0);
            break;
        }
    }

    if(ctx->segs[ctx->i].kind != SEG_END)
        ctx->i += 1; // Skip the final token.

    return 1;
}

bool xtmpl2(const char *tmpl, long len, xt_callback callback, void *userp, const char **err)
{
    *err = NULL;
    Segment *segs = tokenize(tmpl, len, err);
    if(segs == NULL) {
        assert(*err != NULL);
        return 0;
    }
/*
    fprintf(stderr, "# -- Tokens -- #\n");
    for(int i = 0; segs[i].kind != SEG_END; i += 1) {
        static const char *map[] = {
            [SEG_IF] = "if",
            [SEG_FOR] = "for",
            [SEG_ELSE] = "else",
            [SEG_EXPR] = "expr",
            [SEG_TEXT] = "text",
            [SEG_ENDIF] = "endif",
            [SEG_ENDFOR] = "endfor",
        };

        fprintf(stderr, "%s: [%.*s]\n", 
            map[segs[i].kind], 
             (int) segs[i].len, 
            tmpl + segs[i].off);
    }
    fprintf(stderr, "# ------------ #\n");
*/
    RenderContext ctx = {
             .err = NULL,
            .segs = segs,
            .tmpl = tmpl,
             .len = len,
               .i = 0,
           .userp = userp,
        .callback = callback,
    };

    bool ok = render(&ctx, SEG_END);
    assert((ok && ctx.err == NULL) || (!ok && ctx.err != NULL));

    if(!ok && err != NULL)
        *err = ctx.err;

    free(segs);
    return ok;
}

typedef struct {
    bool  failed;
    char *data;
    long  size;
    long  used;
} buff_t;

static void callback(const char *str, long len, void *userp)
{
    buff_t *buff = userp;
    if(buff->failed)
        return;

    if(buff->used + len > buff->size) {

        long new_size;
        if(buff->size == 0) 
            new_size = 1024-1;
        else
            new_size = buff->size * 2;

        if(buff->used + len > new_size)
            new_size = buff->used + len;

        void *temp = realloc(buff->data, new_size+1);
        if(temp == NULL) {
            buff->failed = 1;
            return;
        }

        buff->data = temp;
        buff->size = new_size;
    }

    memcpy(buff->data + buff->used, str, len);
    buff->used += len;
}

char *xtmpl(const char *tmpl, long len, long *outlen, const char **err)
{
    buff_t buff;
    memset(&buff, 0, sizeof(buff_t));
    
    if(!xtmpl2(tmpl, len, callback, &buff, err)) {
        free(buff.data);
        return NULL;
    }

    if(buff.failed) {
        *err = "Out of memory";
        free(buff.data);
        return NULL;
    }

    char *out_str;
    long  out_len;

    if(buff.used == 0) {
        
        out_str = malloc(1);
        if(out_str == NULL) {
            *err = "Out of memory";
            return NULL;
        }
        out_len = 0;

    } else {

        out_str = buff.data;
        out_len = buff.used;
    }

    out_str[out_len] = '\0';

    if(outlen)
        *outlen = out_len;
    return out_str;
}