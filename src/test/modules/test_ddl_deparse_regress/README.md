# Testing harness for DDL deparser

## Testing goals

DDL Deparser provides the ability to encode the original DDL command to a JSON string, then decode it to a fully schema-qualified DDL command which is supposed to have the same effect as the original command. This testing module aims to achieve the following four testing for the DDL deparser:

1. Test that the generated JSON blob is expected using SQL tests.
2. Test that the re-formed DDL command is expected using SQL tests.
3. Test that the re-formed DDL command has the same effect as the original command
   by comparing the results of pg_dump, using the SQL tests in 1 and 2.
4. Test that new DDL syntax is handled by the DDL deparser by capturing and deparing
   DDL commands ran by pg_regress.

1 and 2 is tested with SQL tests, by noticing the deparsed JSON blob and the re-formed command.

Goal 3 is tested with TAP framework in t/001_compare_dumped_results.pl

Goal 4 is tested with TAP framework and pg_regress in 002_regress_tests.pl (Not enabled)

## Usage

Run `make check`, it will run the SQL tests first, then it will run the TAP tests. The execution of 002_regress_tests.pl is currently commented out because it will fail due to unimplemented commands in the DDL deparser.

## How to add more test cases and find out the failure?

You can add test cases to existed files in `sql` folder directly. If you need to create a new test file, you can create a file in `sql` folder, add that test file name to `meson.build` and `Makefile` following the convention used by other test files.

After you have added you test cases, run `make check` and check goal 1 and goal 2 of the added test cases in `results` folder, if the result is right, copy that file to `expected` folder.

Now SQL tests should pass, run `make check` again to check the TAP tests. If everything passed, this test case also meet goal 3. If it fails, check the error message to locate the failure position.

You can find execution logs are in `tmp_check/log` folder, dumped results are in `tmp_check/dumps` folder, reformed sql commands are in `tmp_check/ddl` folder for further investigation.






