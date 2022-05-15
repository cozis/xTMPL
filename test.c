#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "xtmpl.h"

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
    {__LINE__, "{%%}",    NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{% %}",   NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{%@%}",   NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, "{% @ %}", NULL, "block {% .. %} doesn't start with a keyword"},
    {__LINE__, 
        "{% if %}{% if %}{% if %}{% if %}"
        "{% if %}{% if %}{% if %}{% if %}"
        "{% if %}{% if %}{% if %}{% if %}", 
        NULL,
        "Too many nested if-else and for blocks"},
    {__LINE__, 
        "{% for %}{% for %}{% for %}{% for %}"
        "{% for %}{% for %}{% for %}{% for %}"
        "{% for %}{% for %}{% for %}{% for %}", 
        NULL,
        "Too many nested if-else and for blocks"},
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
    {__LINE__, .src = "{{@}}",     .err = "Unexpected character where a primary expression was expected"},
    {__LINE__, .src = "{{ @ }}",   .err = "Unexpected character where a primary expression was expected"},
    {__LINE__, .src = "{{  @  }}", .err = "Unexpected character where a primary expression was expected"},
};

int main()
{
    long total = sizeof(tcases)/sizeof(tcases[0]);
    long passed = 0;
    for(int i = 0; i < total; i += 1) {
        const char *src = tcases[i].src;
        const char *exp = tcases[i].exp;
        const char *exp_err = tcases[i].err;
        const char *err;
        char *res = xtmpl(src, strlen(src), NULL, &err);
        if(exp == NULL && res == NULL) {
        
            // Test was expected to fail and it failed!
            
            if(!strcmp(err, exp_err)) {

                fprintf(stderr, "Test %d: Passed\n", i+1);
                passed += 1;
            
            } else {

                fprintf(stderr, 
                    "Test %d: Failed\n"
                    "\tTemplate:\n"
                    "\t\t%s\n"
                    "\tfailed with error:\n"
                    "\t\t%s\n"
                    "\tbut it was expected to fail with error:\n"
                    "\t\t%s\n", i+1, src, err, exp_err);
            }

        } else if(exp == NULL && res != NULL) {
            
            // Test was expected to fail but it didn't!
            fprintf(stderr, 
                "Test %d: Failed\n"
                "\tTemplate:\n"
                "\t\t%s\n"
                "\twas expected to fail with error:\n"
                "\t\t%s\n"
                "\tbut didn't. Instead, it rendered to:\n"
                "\t\t%s\n", i+1, src, exp_err, res);

            free(res);

        } else if(exp != NULL && res == NULL) {

            // Test was expected to succeed but it didn't

            fprintf(stderr, 
                "Test %d: Failed\n"
                "\tTemplate:\n"
                "\t\t%s\n"
                "\tfailed. It should have rendered:\n"
                "\t\t%s\n"
                "\tThe reported error is:\n"
                "\t\t%s\n", i+1, src, exp, err);

            free(res);

        } else {

            assert(exp != NULL && res != NULL);

            if(!strcmp(exp, res)) {

                fprintf(stderr, "Test %d: Passed\n", i);
                passed += 1;
            
            } else {

                fprintf(stderr, 
                    "Test %d: Failed\n"
                    "\tTemplate:\n"
                    "\t\t%s\n"
                    "\tRendered:\n"
                    "\t\t%s\n"
                    "\tBut it should have rendered:\n"
                    "\t\t%s\n", i+1, src, res, exp);
            }

            free(res);
        }
    }

    fprintf(stdout, "\nTotal: %ld, Passed: %ld, Failed: %ld\n", 
        total, passed, total-passed);
    return 0;
}