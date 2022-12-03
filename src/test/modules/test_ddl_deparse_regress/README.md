# Testing harness for DDL deparser

## Testing goals

DDL Deparser provides ability to encode the original DDL command to a JSON string, then decode it to a DDL command which has the same effect as the original one. So the testing harness will achieve 4 testing goals

1, Compare the generated JSON string with expected JSON string
2, Compare the regenerated DDL command with expected DDL command
3, Compare original DDL dumped results commands and the generated DDL dumped results
4, Run core regression test and test goal 3

Goal 1,2 is tested with normal SQL tests. In the output, after each command execution, it will notice the deparsed JSON and regenerated command.

Goal 3 is tested with TAP framework in t/001_compare_dumped_results.pl

Goal 4 is tested with TAP framework and pg_regress in 002_regress_tests.pl

## Usage

Run `make check`, it will run the Goal 1,2 tests first, then it will run Goal 3 test. For Goal 4 test, I commented the execution because there is an error in ddl_deparse.c which will shutdown the database, need to fix the error.
