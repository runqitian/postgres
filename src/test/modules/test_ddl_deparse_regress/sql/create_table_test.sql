-- part 1: shared prefixes 
-- [ GLOBAL | LOCAL ] { TEMPORARY | TEMP } | UNLOGGED ]
CREATE TABLE part1_simple_table(
    id int,
    name varchar
);
CREATE TEMPORARY TABLE part1_temp_table0(
    id int,
    name varchar
);
CREATE TEMP TABLE part1_temp_table(
    id int,
    name varchar
);
-- GLOBAL TEMP TATBLE is deprecated, expect warning message and create local temp table
CREATE GLOBAL TEMP TABLE part1_global_temp_table(
    id int,
    name varchar
);
CREATE LOCAL TEMP TABLE part1_local_temp_table(
    id int,
    name varchar
);
CREATE UNLOGGED TABLE part1_unlogged_table(
    id int,
    name varchar
);
-- [ IF NOT EXISTS ]
CREATE TABLE IF NOT EXISTS part1_simple_table(
    id int,
    name varchar
);
CREATE TABLE IF NOT EXISTS part1_local_temp_table_not_exists(
    id int,
    name varchar
);

-- part 2: shared suffixes
-- [ PARTITION BY { RANGE | LIST | HASH } ( { column_name | ( expression ) } [ COLLATE collation ] [ opclass ] [, ... ] ) ]
CREATE TABLE part2_partition_by_range_simple(
    id int, 
    name varchar
) PARTITION BY RANGE (id);
CREATE TABLE part2_partition_by_list_simple(
    id int, 
    name varchar
) PARTITION BY LIST (id);
CREATE TABLE part2_partition_by_hash_simple(
    id int, 
    name varchar
) PARTITION BY HASH (id);
CREATE TABLE part2_partition_with_expression(
    id int,
    name varchar
) PARTITION BY RANGE ((id * 190), name);
CREATE TABLE part2_partition_with_collation_opclass(
    id int,
    name varchar
) PARTITION BY RANGE (id int4_ops, name COLLATE "es_ES" varchar_ops);

-- [ USING method ]
-- default method
CREATE TABLE part2_using_default_access_method(
    id int,
    name varchar
) USING heap;

-- [ WITH ( storage_parameter [= value] [, ... ] ) | WITHOUT OIDS ]
CREATE TABLE part2_without_oids(
    id int,
    name varchar
) WITHOUT OIDS;
CREATE TABLE part2_with_one_storage_param(
    id int,
    name varchar
) WITH (fillfactor = 20);
CREATE TABLE part2_with_multiple_storage_params(
    id int,
    name varchar
) WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true);

-- [ ON COMMIT { PRESERVE ROWS | DELETE ROWS | DROP } ]
CREATE TEMP TABLE part2_on_commit_preserve_rows(
    id int,
    name varchar
) ON COMMIT PRESERVE ROWS;
CREATE TEMP TABLE part2_on_commit_delete_rows(
    id int,
    name varchar
) ON COMMIT DELETE ROWS;
CREATE TEMPORARY TABLE part2_on_commit_drop(
    id int,
    name varchar
) ON COMMIT DROP;

-- [ TABLESPACE tablespace_name ]
CREATE TABLE part2_tablespace_pg_default(
    id int,
    name varchar
) TABLESPACE pg_default;

-- some complex combinations from the components above

-- TOFIX: failure case 1 in CREATE TABLE in https://quip-amazon.com/lWMEADkOt12v/DDL-Deparser-testing-failed-cases
-- CREATE TEMPORARY TABLE part2_combination_1(
--     id int,
--     name varchar
-- ) PARTITION BY RANGE (id) ON COMMIT PRESERVE ROWS;

-- TOFIX: failure case 2 in CREATE TABLE in https://quip-amazon.com/lWMEADkOt12v/DDL-Deparser-testing-failed-cases
-- CREATE LOCAL TEMP TABLE part2_combination_2(
--     id int,
--     name varchar
-- ) USING heap WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true) ON COMMIT PRESERVE ROWS TABLESPACE pg_default;

-- TOFIX: failure case 3 in CREATE TABLE in https://quip-amazon.com/lWMEADkOt12v/DDL-Deparser-testing-failed-cases
-- CREATE TABLE part2_combination_3(
--     id int,
--     name varchar
-- ) USING heap WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true) TABLESPACE pg_default;

-- part 3: column constraint, index_parameters
-- [ CONSTRAINT constraint_name ]
-- { NOT NULL |
--  NULL |
--  CHECK ( expression ) [ NO INHERIT ] |
--  DEFAULT default_expr |
--  GENERATED ALWAYS AS ( generation_expr ) STORED |
--  GENERATED { ALWAYS | BY DEFAULT } AS IDENTITY [ ( sequence_options ) ] |
--  UNIQUE [ NULLS [ NOT ] DISTINCT ] index_parameters |
--  PRIMARY KEY index_parameters |
--  REFERENCES reftable [ ( refcolumn ) ] [ MATCH FULL | MATCH PARTIAL | MATCH SIMPLE ]
--    [ ON DELETE referential_action ] [ ON UPDATE referential_action ] }
--[ DEFERRABLE | NOT DEFERRABLE ] [ INITIALLY DEFERRED | INITIALLY IMMEDIATE ]

-- single command can be deparsed into multiple commands

CREATE TABLE part3_not_null_constraint(
    id int CONSTRAINT id_constraint NOT NULL,
    name varchar
);
CREATE TABLE part3_null_constraint(
    id int NULL,
    name varchar CONSTRAINT name_constraint NOT NULL
);
CREATE TABLE part3_check_constraint(
    id int CHECK (id > 10),
    name varchar NOT NULL
);
CREATE TABLE part3_check_constraint_no_inherit(
    id int CHECK (id > 10) NO INHERIT,
    name varchar NOT NULL
);
CREATE TABLE part3_default_constraint(
    id int NOT NULL,
    name varchar DEFAULT 'foo'
);
CREATE TABLE part3_generated_always_as_constraint(
    id int NOT NULL,
    id_generated int GENERATED ALWAYS AS ( id * 10 ) STORED
);
CREATE TABLE part3_generated_always_as_identity_constraint(
    id int NOT NULL,
    id_generated int GENERATED ALWAYS AS IDENTITY
);
CREATE TABLE part3_generated_by_default_as_identity_with_options_constraint(
    id int NOT NULL,
    id_generated int GENERATED BY DEFAULT AS IDENTITY ( INCREMENT BY 10 )
);
CREATE TABLE part3_unique_constraint(
    id int NOT NULL,
    name varchar UNIQUE
);
CREATE TABLE part3_unique_nulls_distinct_constraint(
    id int NOT NULL,
    name varchar UNIQUE NULLS DISTINCT
);
CREATE TABLE part3_unique_nulls_not_distinct_constraint(
    id int NOT NULL,
    name varchar UNIQUE NULLS NOT DISTINCT
);
CREATE TABLE part3_primary_key_constraint(
    id int PRIMARY KEY,
    name varchar UNIQUE
);
CREATE TABLE part3_reference_table_default(
    id int REFERENCES part3_primary_key_constraint,
    name varchar
);
CREATE TABLE part3_reference_table_column(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name)
);
CREATE TABLE part3_reference_table_column_match_full(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH FULL
);
-- -- ERROR:  MATCH PARTIAL not yet implemented
-- -- DDL deparser test will fail when MATCH PARTIAL is implemented, need an update accordingly
-- DO $$
--     BEGIN
--         CREATE TABLE part3_reference_table_column_match_partial(
--             id int,
--             name varchar REFERENCES part3_primary_key_constraint (name) MATCH PARTIAL
--         );
--     EXCEPTION WHEN OTHERS THEN 
--         RAISE NOTICE 'MATCH PARTIAL is not yet implemented, please update test case when it is implemented';
--     END;
-- $$;
CREATE TABLE part3_reference_table_column_match_simple(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH SIMPLE
);
CREATE TABLE part3_reference_table_column_on_delete_no_action(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE NO ACTION
);
CREATE TABLE part3_reference_table_column_on_delete_restrict(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE RESTRICT
);
CREATE TABLE part3_reference_table_column_on_delete_cascade(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE CASCADE
);
CREATE TABLE part3_reference_table_column_on_delete_set_null(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET NULL
);
CREATE TABLE part3_reference_table_column_on_delete_set_null_with_column(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET NULL (name),
    name2 varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET NULL (name2)
);
CREATE TABLE part3_reference_table_column_on_delete_set_default(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET DEFAULT
);
CREATE TABLE part3_reference_table_column_on_delete_set_default_with_col(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET DEFAULT (name)
);
CREATE TABLE part3_reference_table_column_on_update_no_action(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON UPDATE NO ACTION
);
CREATE TABLE part3_reference_table_column_on_update_restrict(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON UPDATE RESTRICT
);
CREATE TABLE part3_reference_table_column_on_update_cascade(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON UPDATE CASCADE
);
CREATE TABLE part3_reference_table_column_on_update_set_null(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON UPDATE SET NULL
);
CREATE TABLE part3_reference_table_column_on_update_set_default(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON UPDATE SET DEFAULT
);
CREATE TABLE part3_reference_table_column_deferable(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) DEFERRABLE
);
CREATE TABLE part3_reference_table_column_not_deferable(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) NOT DEFERRABLE
);
CREATE TABLE part3_reference_table_column_initially_deferred(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) INITIALLY DEFERRED
);
CREATE TABLE part3_reference_table_column_initially_immediate(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) INITIALLY IMMEDIATE
);
CREATE TABLE part3_reference_table_column_complex_comb_deferrable(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH FULL ON DELETE NO ACTION ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE part3_reference_table_column_complex_comb_undeferrable(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH FULL ON DELETE SET DEFAULT ON UPDATE SET NULL NOT DEFERRABLE
);
