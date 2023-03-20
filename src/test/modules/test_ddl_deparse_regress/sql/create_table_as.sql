-- parent table defintion
CREATE TABLE test_create_table_as_base(
    id INT PRIMARY KEY,
    name VARCHAR,
    description TEXT
);

-- CREATE [ [ GLOBAL | LOCAL ] { TEMPORARY | TEMP } | UNLOGGED ] TABLE [ IF NOT EXISTS ] table_name
CREATE TABLE test_simple_table AS SELECT * FROM test_create_table_as_base;
CREATE TABLE IF NOT EXISTS test_simple_table AS TABLE test_create_table_as_base;
-- expect deparsed json to be null for temp table, CREATE TABLE AS command will not deparse temp table
CREATE TEMPORARY TABLE test_temp_table AS TABLE test_create_table_as_base;
-- create table from temp table should be deparsed successfully
CREATE TABLE test_from_temp_table1 AS TABLE test_temp_table;
CREATE TABLE test_from_temp_table2 AS SELECT id from test_temp_table;

CREATE TABLE test_unlogged_table AS SELECT * FROM test_create_table_as_base;
CREATE TABLE IF NOT EXISTS test_unlogged_table AS SELECT * FROM test_create_table_as_base;

-- query
-- SELECT
CREATE TABLE test_select_query_all AS SELECT * FROM test_create_table_as_base;
CREATE TABLE test_select_query_single_col AS SELECT name FROM test_create_table_as_base;
CREATE TABLE test_select_query_multiple_col AS SELECT name, description FROM test_create_table_as_base;
-- TABLE
CREATE TABLE test_table_query AS TABLE test_create_table_as_base;
-- VALUES
CREATE TABLE test_values_query1(id, order_name) AS VALUES (1, 'order1');
CREATE TABLE test_values_query2(id,order_name, purchase_date) AS VALUES (1, 'value1', '2023-01-01'::date), (1, 'value1', '2023-01-02'::date);
-- EXECUTE
PREPARE valid_records(int) AS
  SELECT name FROM test_create_table_as_base WHERE id > $1;
CREATE TABLE test_execute_query AS EXECUTE valid_records(101);

-- [ USING method ]
CREATE TABLE test_using_method USING heap AS TABLE test_create_table_as_base;

-- [ WITH ( storage_parameter [= value] [, ... ] ) | WITHOUT OIDS ]
CREATE TABLE test_with_storage_params_single WITH (fillfactor = 20) AS SELECT * FROM test_create_table_as_base;
CREATE TABLE test_with_storage_params_multiple WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
    AS SELECT * FROM test_create_table_as_base;
CREATE TABLE test_without_oids WITHOUT OIDS AS SELECT * from test_create_table_as_base;

-- [ ON COMMIT { PRESERVE ROWS | DELETE ROWS | DROP } ]
-- temporary table is not supported to be deparsed

-- [ TABLESPACE tablespace_name ]
CREATE TABLE test_tablespace TABLESPACE pg_default AS SELECT name FROM test_create_table_as_base;

-- [ WITH [ NO ] DATA ]
CREATE TABLE test_with_data AS SELECT * FROM test_create_table_as_base WITH DATA;
CREATE TABLE test_with_no_data AS TABLE test_create_table_as_base WITH NO DATA;

-- complex statements
-- CREATE UNLOGGED TABLE test_complex_statement1
--     USING heap
--     WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
--     TABLESPACE pg_default
--     AS SELECT name, id FROM test_create_table_as_base
--     WITH NO DATA;

-- CREATE TABLE test_complex_statement2
--     USING heap
--     WITHOUT OIDS
--     TABLESPACE pg_default
--     AS TABLE test_create_table_as_base
--     WITH NO DATA;

-- CREATE TABLE test_complex_statement3
--     (id, name, dob)
--     USING heap
--     WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
--     TABLESPACE pg_default
--     AS VALUES (1, 'value1', '2023-01-01'::date), (1, 'value1', '2023-01-02'::date)
--     WITH DATA;







