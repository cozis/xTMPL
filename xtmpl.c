#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include "xtmpl.h"

static void report(XT_Error *err, long off, 
                   const char *fmt, ...)
{
    if(err) {
        va_list va;
        va_start(va, fmt);
        int p = vsnprintf(err->message, 
                          sizeof(err->message), 
                          fmt, va);
        va_end(va);
        assert(p >= 0);
        
        err->off = off;
        err->col = -1;
        err->row = -1;
        err->occurred = true;
        
        if(p < (int) sizeof(err->message)) {
            err->truncated = false;
            err->message[p] = '\0';
        } else {
            err->truncated = true;
            err->message[sizeof(err->message)-1] = '\0';
        }
    }
}

#define MAX_DEPTH 8

typedef enum {
    OID_ADD,
    OID_SUB,
    OID_MUL,
    OID_DIV,
} OperatID;

typedef struct {
    XT_Error *err;
    const char *str;
    long   prev_i, i, len;
    Variables *vars;
} EvalContext;

static Value array_new() {
    Value array;
    array.kind = VK_ARRAY;
    array.as_array.capacity = 0;
    array.as_array.count = 0;
    array.as_array.items = NULL;
    return array;
}

static bool array_append(Value *array, Value item, const char **err)
{
    if(array->kind == VK_ERROR || 
         item.kind == VK_ERROR)
        return 0;

    if(array->kind != VK_ARRAY) {
        if(err) 
            *err = "Can't append to something "
                   "other than an array";
        return 0;
    }

    if(array->as_array.count == array->as_array.capacity) {
        int capacity2;
        if(array->as_array.capacity == 0)
            capacity2 = 32;
        else
            capacity2 = 2 * array->as_array.capacity;

        void *addr = realloc(array->as_array.items, capacity2 * sizeof(Value));
        if(addr == NULL) {
            if(err)
                *err = "Out of memory";
            return 0;
        }
        array->as_array.capacity = capacity2;
        array->as_array.items = addr;
    }

    array->as_array.items[array->as_array.count++] = item;
    return 1;
}

static void value_print(Value val, xt_callback callback, void *userp)
{
    switch(val.kind) {

        case VK_ERROR:
        callback("error", 5, userp);
        break;

        case VK_INT:
        {
            char buf[32];
            long len = snprintf(buf, sizeof(buf), 
                                "%lld", val.as_int);
            assert(len >= 0);
            callback(buf, len, userp);
            break;
        }

        case VK_FLOAT:
        {
            char buf[32];
            long len = snprintf(buf, sizeof(buf), 
                               "%lf", val.as_float);
            assert(len >= 0);
            callback(buf, len, userp);
            break;
        }

        case VK_ARRAY:
        {
            callback("[", 1, userp);
            for(int i = 0; i < val.as_array.count; i += 1) {

                value_print(val.as_array.items[i], callback, userp);
                if(i+1 < val.as_array.count)
                    callback(", ", 2, userp);
            }
            callback("]", 1, userp);
            break;
        }
    }
}

static Value apply(OperatID operat, Value lhs, Value rhs, const char **err)
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

            if(err)
                *err = "Bad \"+\" operand";
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

            if(err)
                *err = "Bad \"-\" operand";
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

            if(err)
                *err = "Bad \"*\" operand";
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

            if(err)
                *err = "Bad \"/\" operand";
            return (Value) { VK_ERROR };
        }
    }

    /* UNREACHABLE */
    assert(0);
    return (Value) { VK_ERROR };
}

static Value eval_inner(EvalContext *ctx);

static Value eval_primary(EvalContext *ctx)
{
    while(ctx->i < ctx->len && isspace(ctx->str[ctx->i]))
        ctx->i += 1;

    if(ctx->i == ctx->len) {
        report(ctx->err, ctx->len, "Expression ended where a primary "
                                   "expression was expected");
        return (Value) { VK_ERROR };
    }

    if(isalpha(ctx->str[ctx->i])) {

        long var_off = ctx->i;
        do ctx->i += 1; while(ctx->i < ctx->len && (isalpha(ctx->str[ctx->i]) || isdigit(ctx->str[ctx->i]) || ctx->str[ctx->i] == '_'));
        long var_len = ctx->i - var_off;

        // Find variable
        Value *found = NULL;
        Variables *vars = ctx->vars;
        while(found == NULL && vars != NULL) {
            long j = 0;
            while(found == NULL && vars->list[j].len > 0) {
                if(vars->list[j].len == var_len && !strncmp(vars->list[j].name, ctx->str + var_off, var_len))
                    found = &vars->list[j].value;
                j += 1;
            }
            vars = vars->parent;
        }

        if(found == NULL) {
            report(ctx->err, var_off, 
                "Undefined variable [%.*s]", 
                var_len, ctx->str + var_off);
            return (Value) {VK_ERROR};
        }

        return *found;

    } else if(isdigit(ctx->str[ctx->i])) {

        long long buff = 0;
        do {
            // TODO: Check overflow
            char u = ctx->str[ctx->i] - '0';
            
            if(buff > (LLONG_MAX - u) / 10) {
                report(ctx->err, ctx->i, "Overflow");
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
    
    } else if(ctx->str[ctx->i] == '[') {

        ctx->i += 1; // Skip '['

        while(ctx->i < ctx->len && isspace(ctx->str[ctx->i]))
            ctx->i += 1;

        if(ctx->i == ctx->len) {
            report(ctx->err, ctx->len, "expression ended inside of an array");
            return (Value) {VK_ERROR};
        }

        const char *errmsg;

        Value array = array_new(&errmsg);
        if(array.kind == VK_ERROR) {
            report(ctx->err, ctx->i, errmsg);
            return array;
        }

        if(ctx->str[ctx->i] != ']')
            while(1) {

                Value val = eval_inner(ctx);
                if(val.kind == VK_ERROR)
                    return val;

                if(!array_append(&array, val, &errmsg)) {
                    report(ctx->err, ctx->i, errmsg);
                    return (Value) {VK_ERROR};
                }

                while(ctx->i < ctx->len && isspace(ctx->str[ctx->i]))
                    ctx->i += 1;

                if(ctx->i == ctx->len) {
                    report(ctx->err, ctx->i, "Expression ended inside of an array");
                    return (Value) {VK_ERROR};
                }
                
                if(ctx->str[ctx->i] == ']')
                    break;

                if(ctx->str[ctx->i] != ',') {
                    report(ctx->err, ctx->i, "Unexpected character [%c] inside of an array", ctx->str[ctx->i]);
                    return (Value) { VK_ERROR };
                }
                
                ctx->i += 1; // Skip ','
            }
        
        assert(ctx->str[ctx->i] == ']');
        ctx->i += 1; // Skip ']'

        return array;
    }

    report(ctx->err, ctx->i, "Unexpected character [%c] where a primary expression was expected", ctx->str[ctx->i]);
    return (Value) { VK_ERROR };
}

static bool next_binary_operat(EvalContext *ctx, OperatID *operat, long *off)
{
    while(ctx->i < ctx->len && isspace(ctx->str[ctx->i]))
        ctx->i += 1;

    if(ctx->i < ctx->len) 
        switch(ctx->str[ctx->i]) {
        
            case '+':
            *off = ctx->i;
            ctx->i += 1;
            *operat = OID_ADD;
            return 1;
            
            case '-':
            *off = ctx->i;
            ctx->i += 1;
            *operat = OID_SUB;
            return 1;

            case '*':
            *off = ctx->i;
            ctx->i += 1;
            *operat = OID_MUL;
            return 1;

            case '/':
            *off = ctx->i;
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
    long operat_off;
    while(next_binary_operat(ctx, &operat, &operat_off) && preced_of(operat) >= min_preced) {

        Value rhs = eval_primary(ctx);
        if(rhs.kind == VK_ERROR) {
            assert(ctx->err->occurred);
            return rhs;
        }

        OperatID operat2;
        long operat2_off;
        while(next_binary_operat(ctx, &operat2, &operat2_off) && (preced_of(operat2) > preced_of(operat) || (is_right_assoc(operat2) && preced_of(operat) == preced_of(operat2)))) {

            ctx->i = operat2_off;

            long preced = preced_of(operat) 
                        + (preced_of(operat2) > preced_of(operat));

            rhs = eval_expr_1(ctx, rhs, preced);
            if(rhs.kind == VK_ERROR) {
                assert(ctx->err->occurred);
                return rhs;
            }
        }
        ctx->i = operat2_off;

        const char *errmsg;
        lhs = apply(operat, lhs, rhs, &errmsg);

        if(lhs.kind == VK_ERROR) {
            assert(!ctx->err->occurred);
            report(ctx->err, operat_off, errmsg);
            return lhs;
        }
    }
    ctx->i = operat_off;
    return lhs;
}

static Value eval_inner(EvalContext *ctx)
{
    return eval_expr_1(ctx, eval_primary(ctx), 0);
}

static Value eval(const char *str, long len, Variables *vars, XT_Error *err)
{
    EvalContext ctx = {
        .vars = vars,
         .err = err,
         .str = str,
         .len = len,
           .i = 0,
    };
    Value val = eval_inner(&ctx);
    if(val.kind == VK_ERROR) {
        assert(err->occurred);
    }
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

typedef struct {
    XT_Error *err;
    const char *tmpl;
    long      i, len;
    Segment    *segs;
    Variables  *vars;
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
            {
                Value val = eval(ctx->tmpl + seg.off, seg.len, ctx->vars, ctx->err);
                if(val.kind == VK_ERROR) {
                    assert(ctx->err == NULL || 
                           ctx->err->occurred == true);

                    // The error offset reported by [print]
                    // is relative to the expression start,
                    // so we need to adjust it to make it
                    // relative to the start of the file.
                    // We need to do it only if there is an
                    // error structure and [print] did report
                    // an error offset (it's not set to a
                    // negative value)
                    if(ctx->err && ctx->err->off >= 0)
                        ctx->err->off += seg.off;
                    return 0;
                }
                value_print(val, ctx->callback, ctx->userp);
                break;
            }

            case SEG_IF:
            {
                const char *expr_str = ctx->tmpl + seg.off;
                long        expr_len = seg.len;

                Value r = eval(expr_str, expr_len, ctx->vars, ctx->err);
                if(r.kind == VK_ERROR) {
                    
                    assert(ctx->err == NULL || 
                           ctx->err->occurred == true);

                    // Like for the SEG_EXPR case, we need
                    // to fix the error offset reported by
                    // [eval]
                    if(ctx->err && ctx->err->off >= 0)
                        ctx->err->off += seg.off;
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
                // Get the name of the iteration variable.
                long   k = seg.off;
                long len = seg.off + seg.len;

                while(k < len && isspace(ctx->tmpl[k]))
                    k += 1;

                if(k == len) {
                    report(ctx->err, k, "For statement ended unexpectedly");
                    return 0;
                }

                if(!isalpha(ctx->tmpl[k]) && ctx->tmpl[k] != '_') {
                    report(ctx->err, k, "Missing iteration variable name after [for] keyword");
                    return 0;
                }

                long key_var_off = k;
                do k += 1;
                while(k < len && (isalpha(ctx->tmpl[k]) || ctx->tmpl[k] == '_'));
                long key_var_len = k - key_var_off;
                
                while(k < len && isspace(ctx->tmpl[k]))
                    k += 1;

                if(k == len) {
                    report(ctx->err, k, "For statement ended unexpectedly");
                    return 0;
                }

                long val_var_off = 0;
                long val_var_len = 0;
                if(ctx->tmpl[k] == ',') {

                    k += 1;

                    while(k < len && isspace(ctx->tmpl[k]))
                        k += 1;

                    if(k == len) {
                        report(ctx->err, k, "For statement ended unexpectedly");
                        return 0;
                    }

                    if(!isalpha(ctx->tmpl[k]) && ctx->tmpl[k] != '_') {
                        report(ctx->err, k, "Missing second iteration variable name after ','");
                        return 0;
                    }

                    val_var_off = k;
                    do k += 1; while(k < len && (isalpha(ctx->tmpl[k]) 
                                             || ctx->tmpl[k] == '_'));
                    val_var_len = k - val_var_off;   
                }

                while(k < len && isspace(ctx->tmpl[k]))
                    k += 1;
                
                if(k == len) {
                    report(ctx->err, k, "For statement ended unexpectedly");
                    return 0;
                }

                // Now the "in" keyword is expected
                if(k+2 >= len 
                    || ctx->tmpl[k]   != 'i' 
                    || ctx->tmpl[k+1] != 'n' 
                    || isalpha(ctx->tmpl[k+2]) 
                    ||  '_' == ctx->tmpl[k+2]) {
                    // NOTE: This may trigger even if there is an
                    // [in] keyword but the statement ends after it.
                    // If that's true, it's not the ideal behaviour.
                    report(ctx->err, k, "Missing in keyword after iteration variable names");
                    return 0;
                }

                k += 2; // Skip the "in"

                long        coll_off = k;
                long        coll_len = len - k;
                const char *coll_str = ctx->tmpl + k;

                Value collection = eval(coll_str, coll_len, ctx->vars, ctx->err);
                if(collection.kind == VK_ERROR) {
                    assert(ctx->err == NULL || 
                           ctx->err->occurred == true);
                    if(ctx->err && ctx->err->off >= 0)
                        ctx->err->off += coll_off;
                    return 0;
                }

                if(collection.kind != VK_ARRAY) {
                    report(ctx->err, coll_off, "Interation subject isn't an array");
                    return 0;
                }

                Variables vars = {
                    ctx->vars,
                    (Variable[]) {
                        { ctx->tmpl + key_var_off, key_var_len, { VK_INT, .as_int = 0 }},
                        { ctx->tmpl + val_var_off, val_var_len, { VK_INT, .as_int = 0 }},
                        { NULL, 0, { VK_INT, .as_int = 0 }},
                    }
                };

                vars.parent = ctx->vars;
                ctx->vars = &vars;
                long start = ctx->i;
                for(int no = 0; no < collection.as_array.count; no += 1) {
                    vars.list[0].value.as_int = no;
                    vars.list[1].value = collection.as_array.items[no];
                    ctx->i = start;
                    if(!render(ctx, SEG_ENDFOR))
                        return 0;
                }
                ctx->vars = ctx->vars->parent;
                break;
            }

            case SEG_END:
            case SEG_ELSE:
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

static Segment *tokenize(const char *tmpl, long len, XT_Error *err)
{
    #define SKIP_SPACES()                \
        while(i < len && (tmpl[i] == ' ' \
            || tmpl[i] == '\t'           \
            || tmpl[i] == '\n'))         \
            i += 1;

    #define SKIP_UNTIL_2(X, Y)    \
        while(i < len             \
            && (i+1 > len         \
            || tmpl[i] != (X)     \
            || tmpl[i+1] != (Y))) \
            i += 1;

    Segment *arr = NULL;
    long capacity = 32;
    long count = 0;

    Kind context_stack[MAX_DEPTH];
    bool      has_else[MAX_DEPTH];
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

                    report(err, i, "Out of memory");
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

            long block_off = i;

            assert(tmpl[i] == '{' && tmpl[i+1] == '%');
            i += 2;

            SKIP_SPACES()
            
            if(i == len || (!isalpha(tmpl[i]) && tmpl[i] != '_')) {
                free(arr);
                report(err, block_off, "block {% .. %} doesn't start with a keyword");
                return NULL;
            }

            long kword_off = i;
            do
                i += 1;
            while(i < len && (isalpha(tmpl[i]) || tmpl[i] == '_'));
            long kword_len = i - kword_off;
            
            switch(kword_len) {
                case 2:
                if(strncmp(tmpl + kword_off, "if", kword_len))
                    goto badkword;
                seg.kind = SEG_IF;
                
                if(depth == MAX_DEPTH) {
                    free(arr);
                    report(err, block_off, "Too many nested {% if .. %} and {% for .. %} blocks");
                    return NULL;
                }
                has_else[depth] = 0;
                context_stack[depth++] = SEG_IF;
                break;

                case 3:
                if(strncmp(tmpl + kword_off, "for", kword_len))
                    goto badkword;
                seg.kind = SEG_FOR;

                if(depth == MAX_DEPTH) {
                    free(arr);
                    report(err, block_off, "Too many nested {% if .. %} and {% for .. %} blocks");
                    return NULL;
                }
                has_else[depth] = 0;
                context_stack[depth++] = SEG_FOR;
                break;

                case 4:
                if(strncmp(tmpl + kword_off, "else", kword_len))
                    goto badkword;

                seg.kind = SEG_ELSE;

                if(depth == 0 || context_stack[depth-1] != SEG_IF) {
                    free(arr);
                    report(err, block_off, "{% else %} has no matching {% if .. %}");
                    return NULL;
                }
                if(has_else[depth]) {
                    free(arr);
                    report(err, block_off, "Can't have multiple {% else %} block relative to one {% if .. %}");
                    return 0;
                }
                break;

                case 5:
                if(strncmp(tmpl + kword_off, "endif", kword_len))
                    goto badkword;
                seg.kind = SEG_ENDIF;

                if(depth == 0 || context_stack[depth-1] != SEG_IF) {
                    free(arr);
                    report(err, block_off, "{% endif %} has no matching {% if .. %}");
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
                    report(err, block_off, "{% endfor %} has no matching {% for .. %}");
                    return NULL;
                }
                depth -= 1;
                break;

                default:
            badkword:
                free(arr);
                report(err, kword_off, "Bad {% .. %} block keyword");
                return NULL;
            }

            seg.off = i;
            SKIP_UNTIL_2('%', '}')
            seg.len = i - seg.off;

            if(i == len)
                break;

            assert(tmpl[i] == '%' && tmpl[i+1] == '}');
            i += 2;
        
        } else {

            assert(tmpl[i] == '{' && tmpl[i+1] == '{');
            i += 2;

            seg.kind = SEG_EXPR;
            seg.off = i;
            SKIP_UNTIL_2('}', '}')
            seg.len = i - seg.off;
            
            if(i < len)
                i += 2; // Skip "}}"
        }

        if(arr == NULL || count == capacity) {
            capacity *= 2;
            void *temp = realloc(arr, capacity*sizeof(Segment));
            if(temp == NULL) {
                free(arr);
                report(err, i, "Out of memory");
                return NULL;
            }
            arr = temp;
        }
        arr[count++] = seg;

    } while(arr[count-1].kind != SEG_END);

    return arr;
}

bool xtmpl2(const char *tmpl, long len, Variables *vars, xt_callback callback, void *userp, XT_Error *err)
{
    memset(err, 0, sizeof(XT_Error));
    Segment *segs = tokenize(tmpl, len, err);
    if(segs == NULL) {
        assert(err->occurred);
        return 0;
    }

    RenderContext ctx = {
             .err = err,
            .vars = vars,
            .segs = segs,
            .tmpl = tmpl,
             .len = len,
               .i = 0,
           .userp = userp,
        .callback = callback,
    };

    bool ok = render(&ctx, SEG_END);
    assert((ok && err->occurred == false) || 
          (!ok && err->occurred == true));

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

char *xtmpl(const char *tmpl, long len, Variables *vars, long *outlen, XT_Error *err)
{
    buff_t buff;
    memset(&buff, 0, sizeof(buff_t));
    
    if(!xtmpl2(tmpl, len, vars, callback, &buff, err)) {
        free(buff.data);
        return NULL;
    }

    if(buff.failed) {
        report(err, -1, "Out of memory");
        free(buff.data);
        return NULL;
    }

    char *out_str;
    long  out_len;

    if(buff.used == 0) {
        
        out_str = malloc(1);
        if(out_str == NULL) {
            report(err, -1, "Out of memory");
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