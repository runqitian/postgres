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
CREATE TABLE part2_partition_with_collation(
    id int,
    name varchar
) PARTITION BY LIST (name COLLATE "fr_FR");
CREATE TABLE part2_partition_with_opclass(
    id int,
    name varchar
) PARTITION BY HASH (id int4_ops, name varchar_ops);
CREATE TABLE part2_partition_with_collation_opclass(
    id int,
    name varchar
) PARTITION BY RANGE ((id * 10) int4_ops, name COLLATE "fr_FR" varchar_ops);

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
-- single command can be deparsed into multiple commands

-- [ CONSTRAINT constraint_name ]
-- { NOT NULL |
CREATE TABLE part3_not_null_constraint(
    id int CONSTRAINT id_constraint NOT NULL,
    name varchar
);

--  NULL |
CREATE TABLE part3_null_constraint(
    id int NULL,
    name varchar CONSTRAINT name_constraint NOT NULL
);

--  CHECK ( expression ) [ NO INHERIT ] |
CREATE TABLE part3_check_constraint(
    id int CHECK (id > 10),
    name varchar NOT NULL
);
CREATE TABLE part3_check_constraint_no_inherit(
    id int CHECK (id > 10) NO INHERIT,
    name varchar NOT NULL
);

--  DEFAULT default_expr |
CREATE TABLE part3_default_constraint(
    id int NOT NULL,
    name varchar DEFAULT 'foo'
);

--  GENERATED ALWAYS AS ( generation_expr ) STORED |
CREATE TABLE part3_generated_always_as_constraint(
    id int NOT NULL,
    id_generated int GENERATED ALWAYS AS ( id * 10 ) STORED
);

--  GENERATED { ALWAYS | BY DEFAULT } AS IDENTITY [ ( sequence_options ) ] |
CREATE TABLE part3_generated_always_as_identity_constraint(
    id int NOT NULL,
    id_generated int GENERATED ALWAYS AS IDENTITY
);
CREATE TABLE part3_generated_by_default_as_identity_with_options_constraint(
    id int NOT NULL,
    id_generated int GENERATED BY DEFAULT AS IDENTITY ( INCREMENT BY 10 )
);

--  UNIQUE [ NULLS [ NOT ] DISTINCT ] |
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

--  PRIMARY KEY |
CREATE TABLE part3_primary_key_constraint(
    id int PRIMARY KEY,
    name varchar UNIQUE
);

--  REFERENCES reftable [ ( refcolumn ) ] [ MATCH FULL | MATCH PARTIAL | MATCH SIMPLE ]
--    [ ON DELETE referential_action ] [ ON UPDATE referential_action ] }
CREATE TABLE part3_reference_table_default(
    id int REFERENCES part3_primary_key_constraint,
    name varchar
);
CREATE TABLE part3_reference_table_column(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name)
);
-- [ MATCH FULL | MATCH PARTIAL | MATCH SIMPLE ]
-- skip testing MATCH PARTIAL, which is treated as a syntax error with message
-- ERROR:  MATCH PARTIAL not yet implemented
CREATE TABLE part3_reference_table_column_match_full(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH FULL
);
CREATE TABLE part3_reference_table_column_match_simple(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH SIMPLE
);

-- [ ON DELETE referential_action ] 
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
    foo varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET NULL (foo)
);
CREATE TABLE part3_reference_table_column_on_delete_set_default(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET DEFAULT
);
CREATE TABLE part3_reference_table_column_on_delete_set_default_with_col(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) ON DELETE SET DEFAULT (name)
);

-- [ ON UPDATE referential_action ]
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
-- complex combinations
CREATE TABLE part3_reference_table_column_complex_combination1(
    id int,
    name varchar REFERENCES part3_primary_key_constraint (name) MATCH FULL ON DELETE NO ACTION ON UPDATE NO ACTION
);
CREATE TABLE part3_reference_table_column_complex_combination2(
    id int REFERENCES part3_primary_key_constraint MATCH FULL ON DELETE SET DEFAULT ON UPDATE SET NULL,
    name varchar
);

-- [ DEFERRABLE | NOT DEFERRABLE ]
CREATE TABLE part3_deferable(
    id int,
    name varchar UNIQUE DEFERRABLE
);
CREATE TABLE part3_not_deferable(
    id int PRIMARY KEY NOT DEFERRABLE,
    name varchar
);

-- [ INITIALLY DEFERRED | INITIALLY IMMEDIATE ]
CREATE TABLE part3_initially_deferred(
    id int PRIMARY KEY INITIALLY DEFERRED,
    name varchar
);
CREATE TABLE part3_initially_immediate(
    id int,
    name varchar UNIQUE INITIALLY IMMEDIATE
);

-- complex combination
CREATE TABLE part3_complex_combination(
    id int,
    name varchar CONSTRAINT name_constraint REFERENCES part3_primary_key_constraint (name) MATCH FULL ON DELETE SET DEFAULT ON UPDATE SET NULL NOT DEFERRABLE INITIALLY IMMEDIATE
);

-- part 4: table constraints
-- [ CONSTRAINT constraint_name ]
-- { CHECK ( expression ) [ NO INHERIT ] |
CREATE TABLE part4_constraint_check_1(
    CONSTRAINT id_constraint CHECK (id > 10),
    id int,
    name varchar
);
CREATE TABLE part4_constraint_check_2(
    id int,
    name varchar,
    CONSTRAINT table_check CHECK (id > 10) NO INHERIT
);
CREATE TABLE part4_constraint_check_no_inherit(
    id int,
    name varchar,
    CHECK (id > 10) NO INHERIT
);

--   UNIQUE [ NULLS [ NOT ] DISTINCT ] ( column_name [, ... ] ) [ INCLUDE ( column_name [, ...]) ] |
CREATE TABLE part4_unique_constraint(
    id int,
    name varchar,
    UNIQUE (id)
);
CREATE TABLE part4_unique_constraint_multicols(
    id int,
    name varchar,
    UNIQUE (id, name)
);
CREATE TABLE part4_unique_nulls_distinct_constraint(
    id int,
    name varchar,
    UNIQUE NULLS DISTINCT (id)
);
CREATE TABLE part4_unique_nulls_not_distinct_constraint(
    id int,
    name varchar,
    UNIQUE NULLS NOT DISTINCT (id, name)
);
CREATE TABLE part4_unique_nulls_distinct_constraint_include(
    id int,
    name varchar,
    UNIQUE NULLS DISTINCT (id) INCLUDE (name)
);
CREATE TABLE part4_unique_nulls_distinct_constraint_include_multi(
    id int,
    name varchar,
    info varchar,
    UNIQUE NULLS DISTINCT (id) INCLUDE (name, info)
);

-- PRIMARY KEY ( column_name [, ... ] ) [ INCLUDE ( column_name [, ...]) ] |
CREATE TABLE part4_primary_key(
    id int,
    name varchar,
    PRIMARY KEY (id)
);
CREATE TABLE part4_primary_key_multicols(
    id int,
    name varchar,
    PRIMARY KEY (id, name)
);
CREATE TABLE part4_primary_key_include(
    id int,
    name varchar,
    PRIMARY KEY (id) INCLUDE (name)
);
CREATE TABLE part4_primary_key_include_multicols(
    id int,
    name varchar,
    info varchar,
    PRIMARY KEY (id) INCLUDE (name, info)
);

--   EXCLUDE [ USING index_method ] ( exclude_element WITH operator [, ... ] ) index_parameters [ WHERE ( predicate ) ] |
CREATE TABLE part4_exclude(
    id int,
    name varchar,
    EXCLUDE (name WITH =)
);
CREATE TABLE part4_exclude_multicols(
    id int,
    name varchar,
    EXCLUDE ((id*10) with =, name WITH =)
);
CREATE TABLE part4_exclude_index_method(
    id int,
    name varchar,
    EXCLUDE USING btree ((id*10) with =, name WITH =)
);
-- [ INCLUDE ( column_name [, ... ] ) ]
CREATE TABLE part4_exclude_with_index_params_include_1(
    id int,
    name varchar,
    EXCLUDE (id WITH =) INCLUDE (name)
);
CREATE TABLE part4_exclude_with_index_params_include_2(
    id int,
    name varchar,
    EXCLUDE (id WITH =) INCLUDE (id, name)
);
-- [ WITH ( storage_parameter [= value] [, ... ] ) ]
CREATE TABLE part4_exclude_with_index_params_storage_1(
    id int,
    name varchar,
    EXCLUDE (id WITH =) WITH (fillfactor = 20)
);
CREATE TABLE part4_exclude_with_index_params_storage_2(
    id int,
    name varchar,
    EXCLUDE (id WITH =) WITH (fillfactor = 20, deduplicate_items = false)
);
-- [ USING INDEX TABLESPACE tablespace_name ]
CREATE TABLE part4_exclude_with_index_params_tablespace(
    id int,
    name varchar,
    EXCLUDE (id WITH =) USING INDEX TABLESPACE pg_default
);
-- index_parameters complex combination
CREATE TABLE part4_exclude_with_index_params_complex(
    id int,
    name varchar,
    EXCLUDE (id WITH =) INCLUDE (id, name) WITH (fillfactor = 20, deduplicate_items = false) USING INDEX TABLESPACE pg_default
);
-- [ WHERE ( predicate ) ]
CREATE TABLE part4_exclude_with_predicate(
    id int,
    name varchar,
    EXCLUDE (id WITH =) WHERE (name<>'foo')
);
-- complex combination for table constraint clauses
CREATE TABLE part4_exclude_complex_combination(
    id int,
    name varchar,
    EXCLUDE USING btree (id WITH =, name WITH =) INCLUDE (id, name) WITH (fillfactor = 20, deduplicate_items = false) USING INDEX TABLESPACE pg_default WHERE (name<>'foo')
);

-- FOREIGN KEY ( column_name [, ... ] ) REFERENCES reftable [ ( refcolumn [, ... ] ) ]
--     [ MATCH FULL | MATCH PARTIAL | MATCH SIMPLE ] [ ON DELETE referential_action ] [ ON UPDATE referential_action ] }
CREATE TABLE part4_foreign_table(
    id int PRIMARY KEY,
    name varchar UNIQUE,
    UNIQUE (id, name)
);
CREATE TABLE part4_foreign_key_simple_1(
    id int,
    name varchar,
    FOREIGN KEY (id) REFERENCES part4_foreign_table
);
CREATE TABLE part4_foreign_key_simple_2(
    id int,
    name varchar,
    FOREIGN KEY (id) REFERENCES part4_foreign_table(id)
);
CREATE TABLE part4_foreign_key_multiple_keys(
    id int,
    name varchar,
    FOREIGN KEY (id, name) REFERENCES part4_foreign_table (id, name)
);

-- some combinations from REFERENCES clause, which is already tested in part 3
CREATE TABLE part4_reference_table_complex_combination1(
    id int,
    name varchar,
    FOREIGN KEY (name) REFERENCES part4_foreign_table (name) MATCH SIMPLE ON DELETE CASCADE ON UPDATE SET NULL
);
CREATE TABLE part4_reference_table_complex_combination2(
    id int,
    name varchar,
    CONSTRAINT table_constraint FOREIGN KEY (id, name) REFERENCES part4_foreign_table (id, name) ON DELETE SET NULL (id, name) ON UPDATE SET DEFAULT
);

-- [ DEFERRABLE | NOT DEFERRABLE ]
CREATE TABLE part4_reference_table_column_deferable(
    id int,
    name varchar,
    UNIQUE (id, name) DEFERRABLE
);
CREATE TABLE part4_reference_table_column_not_deferable(
    id int,
    name varchar,
    PRIMARY KEY (id) NOT DEFERRABLE
);

-- [ INITIALLY DEFERRED | INITIALLY IMMEDIATE ]
CREATE TABLE part4_reference_table_column_initially_deferred(
    id int,
    name varchar,
    UNIQUE (id, name) INITIALLY DEFERRED
);
CREATE TABLE part4_reference_table_column_initially_immediate(
    id int,
    name varchar,
    PRIMARY KEY (id) INITIALLY IMMEDIATE
);

-- complex combinations
CREATE TABLE part4_reference_table_column_complex_combination1(
    id int,
    name varchar,
    CONSTRAINT table_constraint FOREIGN KEY (name) REFERENCES part4_foreign_table (name) MATCH FULL ON DELETE NO ACTION ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE part4_reference_table_column_complex_combination2(
    id int,
    name varchar,
    CONSTRAINT table_constraint PRIMARY KEY (id, name) NOT DEFERRABLE INITIALLY IMMEDIATE
);


-- part 5: LIKE source_table [ like_option ... ]
CREATE TABLE part5_source_table(
    id int,
    name varchar
);
CREATE TABLE part5_source_table2(
    id2 int,
    name2 varchar
);
CREATE TABLE part5_like_simple(
    LIKE part5_source_table
);
-- { INCLUDING | EXCLUDING } { COMMENTS | COMPRESSION | CONSTRAINTS | DEFAULTS | GENERATED | IDENTITY | INDEXES | STATISTICS | STORAGE | ALL }
CREATE TABLE part5_including_all(
    LIKE part5_source_table INCLUDING ALL
);
CREATE TABLE part5_including_comments(
    LIKE part5_source_table INCLUDING COMMENTS
);
CREATE TABLE part5_including_compression(
    LIKE part5_source_table INCLUDING COMPRESSION
);
CREATE TABLE part5_including_defaults(
    LIKE part5_source_table INCLUDING DEFAULTS
);
CREATE TABLE part5_including_generated(
    LIKE part5_source_table INCLUDING GENERATED
);
CREATE TABLE part5_including_identity(
    LIKE part5_source_table INCLUDING IDENTITY
);
CREATE TABLE part5_including_indexes(
    LIKE part5_source_table INCLUDING INDEXES
);
CREATE TABLE part5_including_statistics(
    LIKE part5_source_table INCLUDING STATISTICS
);
CREATE TABLE part5_including_storage(
    LIKE part5_source_table INCLUDING STORAGE
);
CREATE TABLE part5_excluding_all(
    LIKE part5_source_table EXCLUDING ALL
);
CREATE TABLE part5_excluding_comments(
    LIKE part5_source_table EXCLUDING COMMENTS
);
CREATE TABLE part5_excluding_compression(
    LIKE part5_source_table EXCLUDING COMPRESSION
);
CREATE TABLE part5_excluding_defaults(
    LIKE part5_source_table EXCLUDING DEFAULTS
);
CREATE TABLE part5_excluding_generated(
    LIKE part5_source_table EXCLUDING GENERATED
);
CREATE TABLE part5_excluding_identity(
    LIKE part5_source_table EXCLUDING IDENTITY
);
CREATE TABLE part5_excluding_indexes(
    LIKE part5_source_table EXCLUDING INDEXES
);
CREATE TABLE part5_excluding_statistics(
    LIKE part5_source_table EXCLUDING STATISTICS
);
CREATE TABLE part5_excluding_storage(
    LIKE part5_source_table EXCLUDING STORAGE
);
CREATE TABLE part5_like_list(
    LIKE part5_source_table,
    info text,
    LIKE part5_source_table2 INCLUDING ALL,
    CONSTRAINT primary_key_constraint PRIMARY KEY (id)
);

-- part 6: partition specification
-- PARTITION OF parent_table { FOR VALUES partition_bound_spec | DEFAULT }
CREATE TABLE part6_parent_table_range(
    id int,
    name varchar
) PARTITION BY RANGE (id);
CREATE TABLE part6_parent_table_list(
    id int,
    name varchar
) PARTITION BY LIST (id);
CREATE TABLE part6_parent_table_hash(
    id int,
    name varchar
) PARTITION BY HASH (id);
CREATE TABLE part6_partition_default PARTITION OF part6_parent_table_range DEFAULT;
-- FROM ( { partition_bound_expr | MINVALUE | MAXVALUE } [, ...] )
--  TO ( { partition_bound_expr | MINVALUE | MAXVALUE } [, ...] ) |
CREATE TABLE part6_partition_spec_range1 PARTITION OF part6_parent_table_range
FOR VALUES FROM (MINVALUE) TO (2);
CREATE TABLE part6_partition_spec_range2 PARTITION OF part6_parent_table_range
FOR VALUES FROM (3) TO (MAXVALUE);
-- IN ( partition_bound_expr [, ...] ) |
CREATE TABLE part6_partition_spec_list PARTITION OF part6_parent_table_list
FOR VALUES IN (1, (1+2), (4+5));
-- WITH ( MODULUS numeric_literal, REMAINDER numeric_literal )
CREATE TABLE part6_partition_spec_hash PARTITION OF part6_parent_table_hash
FOR VALUES WITH (MODULUS 10, REMAINDER 2);


-- part7: create table
-- all data types
CREATE TABLE part7_source_table(
    src_id int
);
CREATE TABLE part7_data_types(
    var1 int8,
    var2 serial8,
    var3 bit,
    var4 bit[5],
    var5 varbit,
    var6 varbit[5],
    var7 bool,
    var8 box,
    var9 bytea,
    var10 char,
    var11 char[8],
    var12 varchar,
    var13 varchar[5],
    var14 cidr,
    var15 circle,
    var16 date,
    var17 double precision,
    var18 inet,
    var19 int,
    var20 int4,
    var21 interval,
    var22 json,
    var23 jsonb,
    var24 line,
    var25 lseg,
    var26 macaddr,
    var27 money,
    var28 decimal,
    var29 decimal(3,1),
    var30 path,
    var31 pg_lsn,
    var32 pg_snapshot,
    var33 point,
    var34 polygon,
    var35 float4,
    var36 int2,
-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNc25b671e9b4947e3856c95333
    -- var37 serial2,
    -- var38 serial4,
    var39 text,
    var40 time,
    var41 time(3),
    var42 timetz,
    var43 timetz(3),
    var44 timestamp,
    var45 timestamp(3),
    var46 timestamptz,
    var47 timestamptz(3),
    var48 tsquery,
    var49 tsvector,
    var50 txid_snapshot,
    var51 uuid,
    var52 xml
);
CREATE TABLE part7_compression_collate(
    str1 varchar COMPRESSION "pglz",
    str2 varchar COLLATE "fr_FR"
);
CREATE TABLE part7_inherits_parent(
    id int,
    name varchar
)
INHERITS (part7_data_types, part7_compression_collate);

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNf80e358da1f44708a54c402fa
-- CREATE TABLE part7_combine_all_clauses(
--     id varchar(5) COMPRESSION "pglz" COLLATE "fr_FR" CONSTRAINT id_constraint DEFAULT 'foo',
--     PRIMARY KEY (id),
--     LIKE part7_source_table,
--     name varchar
-- )
-- INHERITS (part7_data_types, part7_compression_collate)
-- USING heap 
-- WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
-- TABLESPACE pg_default;

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNf80e358da1f44708a54c402fa
-- CREATE TEMP TABLE part7_combine_all_clauses_temp(
--     id varchar(5) COMPRESSION "pglz" COLLATE "fr_FR" CONSTRAINT id_constraint DEFAULT 'foo',
--     PRIMARY KEY (id),
--     LIKE part7_source_table,
--     name varchar
-- )
-- INHERITS (part7_data_types, part7_compression_collate)
-- USING heap 
-- WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
-- ON COMMIT DELETE ROWS
-- TABLESPACE pg_default;

-- part8: create typed table
CREATE TYPE part8_people_type AS (
    id int,
    name varchar,
    height float4,
    weight float4
);
CREATE TABLE part8_create_typed_table OF part8_people_type;
CREATE TABLE part8_create_typed_table_simple OF part8_people_type(
    weight,
    PRIMARY KEY (id)
);
CREATE TABLE part8_create_typed_table_with_options_constaints OF part8_people_type(
    weight WITH OPTIONS NOT NULL,
    name UNIQUE,
    PRIMARY KEY (id)
);

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNf80e358da1f44708a54c402fa
-- CREATE TABLE part8_create_typed_table_complex_combinations OF part8_people_type(
--     weight WITH OPTIONS NOT NULL,
--     name UNIQUE,
--     PRIMARY KEY (id)
-- )
-- USING heap 
-- WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
-- TABLESPACE pg_default;

-- part9: create table as a partition of parent table, FOR VALUES clause is tested in part 6
CREATE TABLE part9_parent_table_range(
    id int,
    name varchar,
    height float4,
    weight float4
) PARTITION BY RANGE (height);
CREATE TABLE part9_parent_table_list(
    id int,
    name varchar,
    height float4,
    weight float4
) PARTITION BY LIST (name);
CREATE TABLE part9_parent_table_hash(
    id int,
    name varchar,
    height float4,
    weight float4
) PARTITION BY HASH (id);

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNba4db3a8ff9844f6999bc891b
-- CREATE TABLE part9_partition_with_options_constraints
-- PARTITION OF part9_parent_table_range (
--     id PRIMARY KEY,
--     name WITH OPTIONS NOT NULL,
--     CHECK (height > 0)
-- )
-- FOR VALUES FROM (MINVALUE) TO (2);

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNba4db3a8ff9844f6999bc891b
-- CREATE TABLE part9_partition_with_options_constraints_default
-- PARTITION OF part9_parent_table_range (
--     id PRIMARY KEY,
--     name WITH OPTIONS NOT NULL,
--     CHECK (height > 0)
-- ) DEFAULT;

-- TOFIX: https://quip-amazon.com/lWMEADkOt12v#temp:C:RSNf80e358da1f44708a54c402fa
-- CREATE TABLE part9_partition_complex_combinations
-- PARTITION OF part9_parent_table_range (
--     id PRIMARY KEY,
--     name WITH OPTIONS NOT NULL,
--     CHECK (height > 0)
-- )
-- FOR VALUES FROM (3) TO (10)
-- USING heap 
-- WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
-- TABLESPACE pg_default;

