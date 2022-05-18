gcc test.c -o test-cov --coverage -Wall -Wextra -DNDEBUG
./test-cov
gcov test-cov-test.gcda -m 