#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "xtmpl.h"



//#define PRINT_TEST_LINES

static long alloc_count = 0;
static long  free_count = 0;

enum {
    NORMAL,
    FAIL_AT_LINE,
    TRACE_ALLOC_LINES,
} realloc_behaviour = NORMAL;

static long traced_lines[1024]; // Must be greater or equal to
                                // the malloc locations in xtmpl.c
static long traced_lines_count = 0;
static long failing_line = -1;

static void *realloc_override(void *p, size_t n, long line)
{
    if(p == NULL) {
        /* It's a new allocation! */

        if(realloc_behaviour == TRACE_ALLOC_LINES) {

            assert(failing_line == -1);
            assert(traced_lines_count >= 0);
            assert((unsigned int) traced_lines_count < sizeof(traced_lines)/sizeof(traced_lines[0]));
            
            bool already_traced_line = false;
            for(int i = 0; i < traced_lines_count; i += 1)
                if(traced_lines[i] == line) {
                    already_traced_line = true;
                    break;
                }

            if(already_traced_line == false)
                traced_lines[traced_lines_count++] = line;

        } else if(realloc_behaviour == FAIL_AT_LINE) {

            assert(failing_line >= 0);
            if(line == failing_line)
                return NULL;
        }
    }

    void *g = realloc(p, n);

    if(g != NULL && p == NULL)
        alloc_count += 1;

    return g;
}

static void free_override(void *p)
{
    if(p != NULL) {
        free_count += 1;
        free(p);
    }
}

#define  malloc(n)    realloc_override(NULL, n, __LINE__)
#define realloc(p, n) realloc_override(p,    n, __LINE__)
#define    free(p)       free_override(p)

#include "xtmpl.c"

struct {
    long line;
    const char *src;
    const char *exp;
    const char *err;
} tcases[] = {
    {__LINE__, "", "", NULL},
    {__LINE__, "Hello, world!", "Hello, world!", NULL},
    {__LINE__, "{{1}}", "1", NULL},
    {__LINE__, "{{10}}", "10", NULL},
    {__LINE__, "{{1.1}}", "1.100000", NULL},
    {__LINE__, "{{10.10}}", "10.100000", NULL},

    {__LINE__, .src = "{{[]}}",  .exp = "[]"},
    {__LINE__, .src = "{{[1]}}", .exp = "[1]"},
    {__LINE__, .src = "{{[1, 2, 3]}}", .exp = "[1, 2, 3]"},
    {__LINE__, .src = "{{[  1, 2, 3  ]}}", .exp = "[1, 2, 3]"},
    {__LINE__, .src = "{{[}}",  .err = "Expression ended inside of an array"},
    {__LINE__, .src = "{{[1}}", .err = "Expression ended inside of an array"},
    {__LINE__, .src = "{{[1@}}", .err = "Unexpected character [@] inside of an array"},
    {__LINE__, .src = "{% if [0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0,"
                             "0, 0, 0, 0, 0, 0, 0] %}", .exp = "" },
    {__LINE__, .src = "{{2+3}}",     .exp = "5"},
    {__LINE__, .src = "{{2+3.0}}",   .exp = "5.000000"},
    {__LINE__, .src = "{{2.0+3}}",   .exp = "5.000000"},
    {__LINE__, .src = "{{2.0+3.0}}", .exp = "5.000000"},

    {__LINE__, .src = "{{2-3}}",     .exp = "-1"},
    {__LINE__, .src = "{{2-3.0}}",   .exp = "-1.000000"},
    {__LINE__, .src = "{{2.0-3}}",   .exp = "-1.000000"},
    {__LINE__, .src = "{{2.0-3.0}}", .exp = "-1.000000"},

    {__LINE__, .src = "{{2*3}}",     .exp = "6"},
    {__LINE__, .src = "{{2*3.0}}",   .exp = "6.000000"},
    {__LINE__, .src = "{{2.0*3}}",   .exp = "6.000000"},
    {__LINE__, .src = "{{2.0*3.0}}", .exp = "6.000000"},

    {__LINE__, .src = "{{2/3}}",     .exp = "0"},
    {__LINE__, .src = "{{2/3.0}}",   .exp = "0.666667"},
    {__LINE__, .src = "{{2.0/3}}",   .exp = "0.666667"},
    {__LINE__, .src = "{{2.0/3.0}}", .exp = "0.666667"},

    {__LINE__, .src = "{{1+[]}}", .err = "Bad \"+\" operand"},
    {__LINE__, .src = "{{1-[]}}", .err = "Bad \"-\" operand"},
    {__LINE__, .src = "{{1*[]}}", .err = "Bad \"*\" operand"},
    {__LINE__, .src = "{{1/[]}}", .err = "Bad \"/\" operand"},
    
    {__LINE__, .src = "{{2*3+5}}", .exp = "11"},
    {__LINE__, .src = "{{2+3*5}}", .exp = "17"},

    {__LINE__, .src = "{{x}}",     .err = "Undefined variable [x]"},
    {__LINE__, .src = "{{xy}}",    .err = "Undefined variable [xy]"},
    {__LINE__, .src = "{{xy0}}",   .err = "Undefined variable [xy0]"},
    {__LINE__, .src = "{{xy01}}",  .err = "Undefined variable [xy01]"},
    {__LINE__, .src = "{{_xy01}}", .err = "Undefined variable [_xy01]"},
    {__LINE__, .src = "{{xy01_}}", .err = "Undefined variable [xy01_]"},
    {__LINE__, .src = "{{xy_01}}", .err = "Undefined variable [xy_01]"},

    {__LINE__, .src = "{% for %}", .err = "For statement ended unexpectedly"},
    {__LINE__, .src = "{% for @ %}", .err = "Missing iteration variable name after [for] keyword"},
    {__LINE__, .src = "{% for in %}", .err = "Unexpected keyword [in] where an iteration variable name was expected" },
    {__LINE__, .src = "{% for x in [] %}", .exp = "" },
    {__LINE__, .src = "{% for xy0_ in [] %}", .exp = "" },
    {__LINE__, .src = "{% for xy0_, xy0_ in [] %}", .exp = "" },
    {__LINE__, .src = "{% for x in [] %}{% endfor %}", .exp = ""},
    {__LINE__, .src = "{% for xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx%}", 
                .err = "Variable name ["
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx] is too long (the maximum is 31)"},
    {__LINE__, .src = "{% for x, xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx"
                                "xxxxxxxxxxxxxxxx%}", 
                .err = "Variable name ["
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx"
                             "xxxxxxxxxxxxxxxx] is too long (the maximum is 31)"},
    {__LINE__, .src = "{% for x %}", .err = "For statement ended unexpectedly"},
    {__LINE__, .src = "{% for x, %}", .err = "For statement ended unexpectedly"},
    {__LINE__, .src = "{% for x, @ %}", .err = "Missing second iteration variable name after ','"},
    
    {__LINE__, .src = "{% for x, in %}", .err = "Unexpected keyword [in] where an iteration variable name was expected"},
    {__LINE__, .src = "{% for x, if %}", .err = "Unexpected keyword [if] where an iteration variable name was expected"},
    {__LINE__, .src = "{% for x, for %}", .err = "Unexpected keyword [for] where an iteration variable name was expected"},
    {__LINE__, .src = "{% for x, else %}", .err = "Unexpected keyword [else] where an iteration variable name was expected"},
    {__LINE__, .src = "{% for x, endif %}", .err = "Unexpected keyword [endif] where an iteration variable name was expected"},
    {__LINE__, .src = "{% for x, endfor %}", .err = "Unexpected keyword [endfor] where an iteration variable name was expected"},

    {__LINE__, .src = "{% for x, y in %}", .err = "Expression ended where a primary expression was expected"},

    {__LINE__, .src = "{% if 0 %}", .exp = ""},
    {__LINE__, .src = "{% if 0 %}{% endif %}", .exp = ""},

    {__LINE__, .src = "{% if 0 %}x{% for x in [0] %}y{% if 0 %}z", .exp = ""},

    {__LINE__, "{%%}",    NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{% %}",   NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{%@%}",   NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{% @ %}", NULL, "block {% .. %} doesn't start with a keyword"},
   
    {__LINE__, 
        .src = "{% if %}{% if %}{% if %}{% if %}"
               "{% if %}{% if %}{% if %}{% if %}"
               "{% if %}{% if %}{% if %}{% if %}", 
        .err = "Too many nested {% if .. %} and {% for .. %} blocks"},
   
    {__LINE__, 
        .src = "{% for %}{% for %}{% for %}{% for %}"
               "{% for %}{% for %}{% for %}{% for %}"
               "{% for %}{% for %}{% for %}{% for %}", 
        .err = "Too many nested {% if .. %} and {% for .. %} blocks"},
   
    {__LINE__, .src = "{% else %}", .err = "{% else %} has no matching {% if .. %}"},
    {__LINE__, .src = "{% endif %}", .err = "{% endif %} has no matching {% if .. %}"},
    {__LINE__, .src = "{% endfor %}", .err = "{% endfor %} has no matching {% for .. %}"},

    {__LINE__, .src = "{% x %}", .err = "Bad {% .. %} block keyword"},
    {__LINE__, .src = "{% xx %}", .err = "Bad {% .. %} block keyword"},
    {__LINE__, .src = "{% xxx %}", .err = "Bad {% .. %} block keyword"},
    {__LINE__, .src = "{% xxxx %}", .err = "Bad {% .. %} block keyword"},
    {__LINE__, .src = "{% xxxxx %}", .err = "Bad {% .. %} block keyword"},
    {__LINE__, .src = "{% xxxxxx %}", .err = "Bad {% .. %} block keyword"},

    {__LINE__, .src = "{{}}",   .err = "Expression ended where a primary expression was expected"},    
    {__LINE__, .src = "{{ }}",  .err = "Expression ended where a primary expression was expected"},
    {__LINE__, .src = "{{  }}", .err = "Expression ended where a primary expression was expected"},
    {__LINE__, .src = "{{20000000000000000000}}", .err = "Overflow"},
    {__LINE__, .src = "{{@}}",     .err = "Unexpected character [@] where a primary expression was expected"},
    {__LINE__, .src = "{{ @ }}",   .err = "Unexpected character [@] where a primary expression was expected"},
    {__LINE__, .src = "{{  @  }}", .err = "Unexpected character [@] where a primary expression was expected"},
    {__LINE__, .src = "{% if 0 %}{% else %}{% else %}", .err = "Can't have multiple {% else %} blocks relative to only one {% if .. %}"},
};

int main()
{
    long total = 0;
    long passed = 0;
    long tcases_num = sizeof(tcases)/sizeof(tcases[0]);

    realloc_behaviour = NORMAL;

    for(int i = 0; i < tcases_num; i += 1) {
        
        total += 1;

        const char *src = tcases[i].src;
        const char *exp = tcases[i].exp;
        const char *exp_err = tcases[i].err;
#ifdef PRINT_TEST_LINES
            fprintf(stderr, "(Line: %ld) ", tcases[i].line);
#endif
        alloc_count = 0;
        free_count = 0;

        XT_Error err;
        char *res = xt_render_str_to_str(src, -1, NULL, NULL, &err);

        long expected_free_count = alloc_count;
        if(res != NULL) expected_free_count -= 1;

        if(free_count != expected_free_count) {

            fprintf(stderr, "Test %ld: Failed\n"
                            "\t%ld memory leaks detected\n", 
                    total, expected_free_count - free_count);

        } else if(exp == NULL && res == NULL) {
        
            // Test was expected to fail and it failed!
            
            if(!strcmp(err.message, exp_err)) {
                fprintf(stderr, "Test %ld: Passed\n", total);
                passed += 1;
            
            } else {

                fprintf(stderr, 
                    "Test %ld: Failed\n"
                    "\tTemplate:\n"
                    "\t\t%s\n"
                    "\tfailed with error:\n"
                    "\t\t%s\n"
                    "\tbut it was expected to fail with error:\n"
                    "\t\t%s\n", total, src, err.message, exp_err);
            }

        } else if(exp == NULL && res != NULL) {
            
            // Test was expected to fail but it didn't!
            fprintf(stderr, 
                "Test %ld: Failed\n"
                "\tTemplate:\n"
                "\t\t%s\n"
                "\twas expected to fail with error:\n"
                "\t\t%s\n"
                "\tbut didn't. Instead, it rendered to:\n"
                "\t\t%s\n", total, src, exp_err, res);

            free(res);

        } else if(exp != NULL && res == NULL) {

            // Test was expected to succeed but it didn't

            fprintf(stderr, 
                "Test %ld: Failed\n"
                "\tTemplate:\n"
                "\t\t%s\n"
                "\tfailed. It should have rendered:\n"
                "\t\t%s\n"
                "\tThe reported error is:\n"
                "\t\t%s\n", total, src, exp, err.message);

            free(res);

        } else {

            assert(exp != NULL && res != NULL);

            if(!strcmp(exp, res)) {
                fprintf(stderr, "Test %ld: Passed\n", total);
                passed += 1;
            
            } else {

                fprintf(stderr, 
                    "Test %ld: Failed\n"
                    "\tTemplate:\n"
                    "\t\t%s\n"
                    "\tRendered:\n"
                    "\t\t%s\n"
                    "\tBut it should have rendered:\n"
                    "\t\t%s\n", total, src, res, exp);
            }

            free(res);
        }
    }

    /* Now trace all of the allocation lines */

    realloc_behaviour = TRACE_ALLOC_LINES;
    traced_lines_count = 0;

    for(int i = 0; i < tcases_num; i += 1) {
        
        const char *src = tcases[i].src;

        alloc_count = 0;
        free_count = 0;

        XT_Error err;
        char *res = xt_render_str_to_str(src, -1, NULL, NULL, &err);

        if(res != NULL)
            free(res);
    }

    /* Now make the traced lines fail */

    realloc_behaviour = FAIL_AT_LINE;
    for(int j = 0; j < traced_lines_count; j += 1) {

        failing_line = traced_lines[j];

        for(int i = 0; i < tcases_num; i += 1) {
            
            total += 1;
            const char *src = tcases[i].src;

#ifdef PRINT_TEST_LINES
            fprintf(stderr, "(Line: %ld) ", tcases[i].line);
#endif
            alloc_count = 0;
            free_count = 0;

            XT_Error err;
            char *res = xt_render_str_to_str(src, -1, NULL, NULL, &err);

            if(res != NULL)
                free(res);

            if(free_count != alloc_count)
                fprintf(stderr, 
                    "Test %ld: Failed\n"
                    "\tDetected %ld memory leaks when an allocation failes at line %ld\n", 
                    total, alloc_count-free_count, failing_line);
            else {
                fprintf(stderr, "Test %ld: Passed\n", total);
                passed += 1;
            }

            long expected_free_count = alloc_count;
            if(res != NULL) expected_free_count -= 1;
        }
    }

    fprintf(stdout, "\nTotal: %ld, Passed: %ld, Failed: %ld\n", 
            total, passed, total-passed);

    return 0;
}