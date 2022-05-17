gcc test.c xtmpl.c -o test-cov --coverage -Wall -Wextra
./test-cov
gcov test-cov-xtmpl.gcda -m 