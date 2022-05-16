#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include "xtmpl.h"

typedef enum {
    SK_TEXT,
    SK_EXPR,
    SK_IF,
    SK_FOR,
    SK_ELSE,
    SK_ENDIF,
    SK_ENDFOR,
    SK_END,
} SliceKind;

typedef struct {
    SliceKind kind;
    long  off, len;
} Slice;

typedef struct {
    long   count, 
       max_count;
    Slice list[];
} Slices;

typedef struct {
    XT_Error    *err;
    
    const char  *str;
    long         len;

    long   slice_idx;
    Slices   *slices;
    Variables  *vars;
    void      *userp;
    xt_callback callback;
} RenderContext;

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

static void report(XT_Error *err, long off, 
                   const char *fmt, ...)
{
    if(err) {
        va_list va;
        va_start(va, fmt);
        int p = vsnprintf(err->message, sizeof(err->message), fmt, va);
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

static void value_free(Value *val)
{
    switch(val->kind) {
        default:break;
        case VK_ARRAY: free(val->as_array.items); break;
    }
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

static Value apply(OperatID operat, Value lhs, Value rhs, const char **err)
{
    if(lhs.kind == VK_ERROR) return lhs;
    if(rhs.kind == VK_ERROR) return rhs;

    #define PACK(X, Y, Z)                  \
        (((unsigned long long) X) <<  0) | \
        (((unsigned long long) Y) << 16) | \
        (((unsigned long long) Z) << 32)

    switch(PACK(operat, lhs.kind, rhs.kind)) {
        // TODO: Check overflow.
        case PACK(OID_ADD, VK_INT,   VK_INT):   return (Value) { VK_INT,   .as_int   = lhs.as_int   + rhs.as_int   };
        case PACK(OID_ADD, VK_INT,   VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_int   + rhs.as_float };
        case PACK(OID_ADD, VK_FLOAT, VK_INT):   return (Value) { VK_FLOAT, .as_float = lhs.as_float + rhs.as_int   };
        case PACK(OID_ADD, VK_FLOAT, VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_float + rhs.as_float };

        case PACK(OID_SUB, VK_INT,   VK_INT):   return (Value) { VK_INT,   .as_int   = lhs.as_int   - rhs.as_int   };
        case PACK(OID_SUB, VK_INT,   VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_int   - rhs.as_float };
        case PACK(OID_SUB, VK_FLOAT, VK_INT):   return (Value) { VK_FLOAT, .as_float = lhs.as_float - rhs.as_int   };
        case PACK(OID_SUB, VK_FLOAT, VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_float - rhs.as_float };

        case PACK(OID_MUL, VK_INT,   VK_INT):   return (Value) { VK_INT,   .as_int   = lhs.as_int   * rhs.as_int   };
        case PACK(OID_MUL, VK_INT,   VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_int   * rhs.as_float };
        case PACK(OID_MUL, VK_FLOAT, VK_INT):   return (Value) { VK_FLOAT, .as_float = lhs.as_float * rhs.as_int   };
        case PACK(OID_MUL, VK_FLOAT, VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_float * rhs.as_float };

        case PACK(OID_DIV, VK_INT,   VK_INT):   return (Value) { VK_INT,   .as_int   = lhs.as_int   / rhs.as_int   };
        case PACK(OID_DIV, VK_INT,   VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_int   / rhs.as_float };
        case PACK(OID_DIV, VK_FLOAT, VK_INT):   return (Value) { VK_FLOAT, .as_float = lhs.as_float / rhs.as_int   };
        case PACK(OID_DIV, VK_FLOAT, VK_FLOAT): return (Value) { VK_FLOAT, .as_float = lhs.as_float / rhs.as_float };

        default:
        switch(operat) {
            case OID_ADD: *err = "Bad \"+\" operand"; break;
            case OID_SUB: *err = "Bad \"-\" operand"; break;
            case OID_MUL: *err = "Bad \"*\" operand"; break;
            case OID_DIV: *err = "Bad \"/\" operand"; break;
        }
        break;
    }

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
        do 
            ctx->i += 1; 
        while(ctx->i < ctx->len && (isalpha(ctx->str[ctx->i]) || 
                                    isdigit(ctx->str[ctx->i]) || 
                                    ctx->str[ctx->i] == '_'));
        long var_len = ctx->i - var_off;

        // Find variable
        Value *found = NULL;
        Variables *vars = ctx->vars;
        while(found == NULL && vars != NULL) {
            for(long j = 0; found == NULL && vars->list[j].len > 0; j += 1)

                if(vars->list[j].len == var_len && !strncmp(vars->list[j].name, ctx->str + var_off, var_len))
                    found = &vars->list[j].value;
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
            report(ctx->err, ctx->i, "%s", errmsg);
            return array;
        }

        if(ctx->str[ctx->i] != ']')
            while(1) {

                Value val = eval_inner(ctx);
                if(val.kind == VK_ERROR)
                    return val;

                if(!array_append(&array, val, &errmsg)) {
                    report(ctx->err, ctx->i, "%s", errmsg);
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
    long operat_off = ctx->i;
    while(next_binary_operat(ctx, &operat, &operat_off) && preced_of(operat) >= min_preced) {

        Value rhs = eval_primary(ctx);
        if(rhs.kind == VK_ERROR) {
            assert(ctx->err->occurred);
            return rhs;
        }

        OperatID operat2;
        long operat2_off = ctx->i;
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
            report(ctx->err, operat_off, "%s", errmsg);
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
    if(val.kind == VK_ERROR)
        assert(err == NULL || err->occurred == true);
    return val;
}

static bool parse_for_statement(const char *str, long len, 
                               char *var1, char *var2, long var_max, 
                               long *coll_off, long *coll_len,
                               XT_Error *err)
{
    /* [str] must contain a string in one of the
     * following forms:
     *   A    in C
     *   A, B in C
     *
     * where A and B are variable names and C is an
     * expression.
     *
     * Variable names can contain alphabetical 
     * characters, digits or _, but the first 
     * character can't be a digit).
     *
     * Whitespace between elements must not matter.
     */

    long i = 0;

    // Skip spaces before "A"
    while(i < len && isspace(str[i]))
        i += 1;

    if(i == len) {
        report(err, i, "For statement ended unexpectedly");
        return 0;
    }

    {
        // We expect the first character of "A"
        if(!isalpha(str[i]) && str[i] != '_') {
            report(err, i, "Missing iteration variable name after [for] keyword");
            return 0;
        }

        /* Start of "A" */
        long key_var_off = i;

        do 
            i += 1;
        while(i < len && (isalpha(str[i]) || str[i] == '_'));

        /* End of "A" */
        long key_var_len = i - key_var_off;

        if(key_var_len > var_max-1) {
            report(err, key_var_off, "Variable name [%.*s] is too long. The maximum is %d\n", 
                (int) key_var_len, str + key_var_off, var_max-1);
            return 0;
        }

        memcpy(var1, str + key_var_off, key_var_len);
        var1[key_var_len] = '\0';

        // TODO: Make sure "A" isn't a keyword.
    }

    // Skip spaces before "in" or ','
    while(i < len && isspace(str[i]))
        i += 1;

    if(i == len) {
        report(err, i, "For statement ended unexpectedly");
        return 0;
    }

    // If now comes either the "in" keyword or
    // ',' if "B" was also specified.
    
    if(str[i] == ',') {

        i += 1; // Skip ','

        // Skip spaces before "B"
        while(i < len && isspace(str[i]))
            i += 1;

        if(i == len) {
            report(err, i, "For statement ended unexpectedly");
            return 0;
        }

        /* Start of "B" */

        if(!isalpha(str[i]) && str[i] != '_') {
            report(err, i, "Missing second iteration variable name after ','");
            return 0;
        }

        long val_var_off = i;
        do i += 1; while(i < len && (isalpha(str[i]) 
                                 || str[i] == '_'));
        /* End of B */
        long val_var_len = i - val_var_off;

        if(val_var_len > var_max-1) {
            report(err, val_var_off, "Variable name [%.*s] is too long. The maximum is %d\n", 
                (int) val_var_len, str + val_var_off, var_max-1);
            return 0;
        }

        memcpy(var2, str + val_var_off, val_var_len);
        var2[val_var_len] = '\0';
    } else {
        // "B" wasn't specified, just write a null byte
        // to tell the caller it wasn't specified.
        if(var_max > 0)
            var2[0] = '\0';
    }

    {
        // Now skip the spaces before the "in" keyword
        while(i < len && isspace(str[i]))
            i += 1;
        
        if(i == len) {
            report(err, i, "For statement ended unexpectedly");
            return 0;
        }

        // Now the "in" keyword is expected
        if(i+2 >= len 
            || str[i]   != 'i' 
            || str[i+1] != 'n' 
            || isalpha(str[i+2]) 
            ||  '_' == str[i+2]) {
            // NOTE: This may trigger even if there is an
            // [in] keyword but the statement ends after it.
            // If that's true, it's not the ideal behaviour.
            report(err, i, "Missing in keyword after iteration variable names");
            return 0;
        }

        i += 2; // Skip "in"
    }

    *coll_off = i;
    *coll_len = len - i;
    return 1;
}

static void skip_until_or_end(RenderContext *ctx, SliceKind until)
{
    int depth = 0;
    while(ctx->slice_idx < ctx->slices->count) {
        
        Slice slice = ctx->slices->list[ctx->slice_idx++];

        if(depth == 0 && slice.kind == until)
            break;

        switch(slice.kind) {
            default       : break;
            case SK_IF    :
            case SK_FOR   : depth += 1; break;
            case SK_ENDIF :
            case SK_ENDFOR: depth -= 1; break;
        }
    }
}

static bool render(RenderContext *ctx, SliceKind until)
{
    while(ctx->slice_idx < ctx->slices->count && 
          ctx->slices->list[ctx->slice_idx].kind != until) {

        Slice slice = ctx->slices->list[ctx->slice_idx++];

        switch(slice.kind) {
            
            case SK_TEXT:
            ctx->callback(ctx->str + slice.off, slice.len, ctx->userp);
            break;

            case SK_EXPR:
            {
                Value val = eval(ctx->str + slice.off, slice.len, 
                                 ctx->vars, ctx->err);

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
                        ctx->err->off += slice.off;
                    return 0;
                }
                value_print(val, ctx->callback, ctx->userp);
                value_free(&val);
                break;
            }

            case SK_IF:
            {
                Value r = eval(ctx->str + slice.off, slice.len, 
                               ctx->vars, ctx->err);

                if(r.kind == VK_ERROR) {
                    
                    assert(ctx->err == NULL || 
                           ctx->err->occurred == true);

                    // Like for the SK_EXPR case, we need
                    // to fix the error offset reported by
                    // [eval]
                    if(ctx->err && ctx->err->off >= 0)
                        ctx->err->off += slice.off;
                    return 0;
                }

                bool returned_0 = (r.kind == VK_INT && r.as_int == 0);

                value_free(&r);

                if(!returned_0) {
                    
                    /* -- Took the IF branch -- */
                    
                    // Execute until the {% else %}
                    if(!render(ctx, SK_ELSE))
                        return 0;

                    // Now skip to the token after the {% endif %}
                    skip_until_or_end(ctx, SK_ENDIF);

                } else {

                    /* -- Took the ELSE branch -- */

                    // Now skip to the token after the {% else %}
                    skip_until_or_end(ctx, SK_ELSE);

                    if(!render(ctx, SK_ENDIF))
                        return 0;
                }
                break;
            }

            case SK_FOR:
            {
                char var1[32];
                char var2[32];
                assert(sizeof(var1) == sizeof(var2));

                long coll_off, coll_len;
                if(!parse_for_statement(ctx->str + slice.off, slice.len, 
                                        var1, var2, sizeof(var1), 
                                        &coll_off, &coll_len, ctx->err)) {
                    if(ctx->err && ctx->err->off >= 0)
                        ctx->err->off += slice.off;
                    return 0;
                }

                // Note that [coll_off] is relative to the
                // for statement body, not the start of the
                // source string. We need to ass [slice.off]
                // to use it here.
                const char *coll_str = ctx->str + slice.off + coll_off;

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
                        { var1, strlen(var1), { VK_INT, .as_int = 0 }},
                        { var2, strlen(var2), { VK_INT, .as_int = 0 }},
                        { NULL, 0, { VK_INT, .as_int = 0 }},
                    }
                };

                vars.parent = ctx->vars;
                ctx->vars = &vars;
                
                long count = collection.as_array.count;
    
                if(count == 0)
                    skip_until_or_end(ctx, SK_ENDFOR);
                else {
    
                    long start = ctx->slice_idx;
                    for(int no = 0; no < count; no += 1) {
                        
                        vars.list[0].value.as_int = no;
                        vars.list[1].value = collection.as_array.items[no];

                        ctx->slice_idx = start;
                        
                        if(!render(ctx, SK_ENDFOR)) {
                            value_free(&collection);
                            return 0;
                        }
                    }
                }
                value_free(&collection);
                value_free(&vars.list[0].value);
                value_free(&vars.list[1].value);
                ctx->vars = ctx->vars->parent;
                break;
            }

            case SK_END:
            case SK_ELSE:
            case SK_ENDIF:
            case SK_ENDFOR:
            /* Unreachable */
            assert(0);
            break;
        }
    }

    if(ctx->slice_idx < ctx->slices->count)
        ctx->slice_idx += 1;
    return 1;
}

static bool append_slice(Slices **slices, Slice slice)
{
    Slices *slices2 = *slices;

    if(slices2->count == slices2->max_count) {
        
        int new_max_count = (slices2->max_count == 0) 
                          ? 32 : 2 * slices2->max_count;

        void *temp = realloc(*slices, sizeof(Slices) + new_max_count * sizeof(Slice));
        if(temp == NULL)
            return 0;

        slices2 = temp;
        slices2->max_count = new_max_count;
        *slices = slices2;
    }

    slices2->list[slices2->count++] = slice;
    return 1;
}

static Slices *slice_up(const char *tmpl, long len, XT_Error *err)
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

    Slices *slices = malloc(sizeof(Slices));
    if(slices == NULL) {
        report(err, 0, "Out of memory");
        goto failed;
    }
    slices->count = 0;
    slices->max_count = 0;

    SliceKind context[MAX_DEPTH];
    bool     has_else[MAX_DEPTH];
    int depth = 0, i = 0;
    while(1) {

        // Slice the raw text before the next {{ .. }}, {% .. %} or,
        // end of the string, then append it to the slice list.
        Slice text;
        text.kind = SK_TEXT;
        text.off = i;
        while(i < len && (i+1 > len 
                      || tmpl[i] != '{' 
                      ||   (tmpl[i+1] != '%' 
                         && tmpl[i+1] != '{')))
            i += 1;
        text.len = i - text.off;
        
        if(text.len > 0)
            if(!append_slice(&slices, text)) {
                report(err, i, "Out of memory");
                goto failed;
            }

        // Did the source end?
        if(i == len)
            break;

        // Now handle either the {{ .. }} or {% .. %}

        assert(tmpl[i] == '{' && (tmpl[i+1] == '%' || tmpl[i+1] == '{'));
        i += 2; // Skip the "{%" or "{{"

        Slice slice;
        if(tmpl[i-1] == '%') {

            assert(tmpl[i-2] == '{' && tmpl[i-1] == '%');
            long block_off = i-2; // Useful for error reporting. This is the
                                  // offset of the first '{' of the {% .. %}
                                  // block.

            // Now skip any spaces between the '%' and
            // the first keyword. If there is no keyword,
            // report the error.

            SKIP_SPACES()
            
            if(i == len || (!isalpha(tmpl[i]) && tmpl[i] != '_')) {
                report(err, block_off, "block {%% .. %%} doesn't start with a keyword");
                goto failed;
            }

            long kword_off = i;
            do
                i += 1;
            while(i < len && (isalpha(tmpl[i]) || tmpl[i] == '_'));
            long kword_len = i - kword_off;
            
            // The keyword is the substring that starts at offset
            // [kword_off] and has length [kword_len].

            // Check that:
            //   - The keyword is valid (if, for, else, endif, ..).
            //   - If it's an if or for, that the maximum depth of
            //     nested blocks isn't reached.
            //   - If it's an endif, it's relative to a previous if.
            //   - If it's an endfor, it's relativo to a previous for.
            //   - If it's an else, it's relative to a previous if
            //     that had no other else associated to it.

            switch(kword_len) {

                case 2:
                if(strncmp(tmpl + kword_off, "if", kword_len))
                    goto badkword;
                
                if(depth == MAX_DEPTH) {
                    report(err, block_off, "Too many nested {%% if .. %%} and {%% for .. %%} blocks");
                    goto failed;
                }
                has_else[depth] = 0;
                context[depth++] = SK_IF;
                slice.kind = SK_IF;
                break;

                case 3:
                if(strncmp(tmpl + kword_off, "for", kword_len))
                    goto badkword;

                if(depth == MAX_DEPTH) {
                    report(err, block_off, "Too many nested {%% if .. %%} and {%% for .. %%} blocks");
                    goto failed;
                }
                has_else[depth] = 0;
                context[depth++] = SK_FOR;
                slice.kind = SK_FOR;
                break;

                case 4:
                if(strncmp(tmpl + kword_off, "else", kword_len))
                    goto badkword;

                if(depth == 0 || context[depth-1] != SK_IF) {
                    report(err, block_off, "{%% else %%} has no matching {%% if .. %%}");
                    goto failed;
                }
                if(has_else[depth-1]) {
                    report(err, block_off, "Can't have multiple {%% else %%} "
                                           "block relative to one {%% if .. %%}");
                    goto failed;
                }

                has_else[depth-1] = true;
                slice.kind = SK_ELSE;
                break;

                case 5:
                if(strncmp(tmpl + kword_off, "endif", kword_len))
                    goto badkword;

                if(depth == 0 || context[depth-1] != SK_IF) {
                    report(err, block_off, "{%% endif %%} has no matching {%% if .. %%}");
                    goto failed;
                }
                depth -= 1;
                slice.kind = SK_ENDIF;
                break;

                case 6:
                if(strncmp(tmpl + kword_off, "endfor", kword_len))
                    goto badkword;

                if(depth == 0 || context[depth-1] != SK_FOR) {
                    report(err, block_off, "{%% endfor %%} has no matching {%% for .. %%}");
                    goto failed;
                }
                depth -= 1;
                slice.kind = SK_ENDFOR;
                break;

                default:
            badkword:
                report(err, kword_off, "Bad {%% .. %%} block keyword");
                goto failed;
            }

            // Now get the slice that goes from the first
            // byte after the keyword until the byte before
            // the '%' of the ending "%}" (or the end of
            // the source).

            slice.off = i;
            SKIP_UNTIL_2('%', '}')
            slice.len = i - slice.off;

        } else {

            assert(tmpl[i-2] == '{' && tmpl[i-1] == '{');

            slice.kind = SK_EXPR;
            slice.off = i;
            SKIP_UNTIL_2('}', '}')
            slice.len = i - slice.off;
        }

        if(i < len) {
            assert((tmpl[i] == '%' || tmpl[i] == '}') && tmpl[i+1] == '}');
            i += 2; // Skip the "%}" or "}}"
        }

        if(!append_slice(&slices, slice)) {
            report(err, i, "Out of memory");
            goto failed;
        }
    }

    return slices;

failed:
    assert(err == NULL || err->occurred == true);
    free(slices);
    return NULL;
}

bool xt_render_str_to_cb(const char *str, long len, Variables *vars, 
                              xt_callback callback, void *userp, XT_Error *err)
{
    if(str == NULL)
        str = "";

    if(len < 0)
        len = strlen(str);

    memset(err, 0, sizeof(XT_Error));
    Slices *slices = NULL;
    bool ok = false;

    slices = slice_up(str, len, err);
    if(slices == NULL) {
        assert(err == NULL || err->occurred == true);
        goto done;
    }

    RenderContext ctx = {
        .err = err,
        .vars = vars,
        .str = str,
        .len = len,
        .slices = slices,
        .slice_idx = 0,
        .userp = userp,
        .callback = callback,
    };

    if(!render(&ctx, SK_END)) {
        assert(err == NULL || err->occurred == true);
        goto done;
    }

    ok = true;
done:
    
    assert((ok && err->occurred == false) || 
          (!ok && err->occurred == true));

    if(!ok && err && err->off >= 0) {
        // Calculate the line and column of the error
        // given the absolute offset and the string.
        assert(err->off < len);

        long col = 1, 
             row = 1;
        long i = 0;
        while(i < err->off) {
            col += 1;
            if(str[i] == '\n') {
                col = 0;
                row += 1;
            }
            i += 1;
        }
        err->col = col;
        err->row = row;
    }

    free(slices);
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

char *xt_render_str_to_str(const char *str, long len, Variables *vars, long *outlen, XT_Error *err)
{
    buff_t buff;
    memset(&buff, 0, sizeof(buff_t));
    
    if(!xt_render_str_to_cb(str, len, vars, callback, &buff, err)) {
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

static char *load_file(const char *file, long *len, const char **err)
{
    FILE *fp  = NULL;
    char *res = NULL;
    long size = 1024, used = 0;

    fp = fopen(file, "rb");
    if(fp == NULL) {
        *err = "Couldn't open file";
        goto failed;
    }

    while(1) {

        size *= 2;
        void *p = realloc(res, size);
        if(p == NULL) {
            *err = "Out of memory";
            goto failed;
        }
        res = p;

        long unused = size - used - 1; // -1 for ending \0
        long n = fread(res + used, 1, unused, fp);
        used += n;
        
        if(n < unused) {

            if(ferror(fp))
                goto failed;

            break;
        }
    }
    res[used] = '\0';
    *len = used;
    fclose(fp);
    return res;

failed:
    assert(*err != NULL);
    if(fp != NULL)
        fclose(fp);
    free(res);
    return NULL;
}

bool xt_render_file_to_cb(const char *file, Variables *vars, 
                               xt_callback callback, void *userp, XT_Error *err)
{
    long len;
    const char *errmsg;
    char *str = load_file(file, &len, &errmsg);
    if(str == NULL) {
        report(err, 0, "%s", errmsg);
        return 0;
    }

    bool ok = xt_render_str_to_cb(str, len, vars, callback, userp, err);

    free(str);
    return ok;
}

char *xt_render_file_to_str(const char *file, Variables *vars, 
                                 long *outlen, XT_Error *err)
{
    long len;
    const char *errmsg;
    char *str = load_file(file, &len, &errmsg);
    if(str == NULL) {
        report(err, 0, "%s", errmsg);
        return 0;
    }

    char *res = xt_render_str_to_str(str, len, vars, outlen, err);

    free(str);
    return res;
}