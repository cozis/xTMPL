gcc test.c -o test-cov --coverage -Wall -Wextra
./test-cov
gcov test-cov-test.gcda -m 