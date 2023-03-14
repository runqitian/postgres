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
CREATE TEMPORARY TABLE part2_combination_1(
    id int,
    name varchar
) PARTITION BY RANGE (id) ON COMMIT PRESERVE ROWS;

CREATE LOCAL TEMP TABLE part2_combination_2(
    id int,
    name varchar
) USING heap WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true) ON COMMIT PRESERVE ROWS TABLESPACE pg_default;

CREATE TABLE part2_combination_3(
    id int,
    name varchar
) USING heap WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true) TABLESPACE pg_default;


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
    var37 serial2,
    var38 serial4,
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

CREATE TABLE part7_combine_all_clauses(
    id varchar(5) COMPRESSION "pglz" COLLATE "fr_FR" CONSTRAINT id_constraint DEFAULT 'foo',
    PRIMARY KEY (id),
    LIKE part7_source_table,
    name varchar
)
INHERITS (part7_data_types, part7_compression_collate)
USING heap
WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
TABLESPACE pg_default;

CREATE TEMP TABLE part7_combine_all_clauses_temp(
    id varchar(5) COMPRESSION "pglz" COLLATE "fr_FR" CONSTRAINT id_constraint DEFAULT 'foo',
    PRIMARY KEY (id),
    LIKE part7_source_table,
    name varchar
)
INHERITS (part7_data_types, part7_compression_collate)
USING heap
WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
ON COMMIT DELETE ROWS
TABLESPACE pg_default;

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

CREATE TABLE part8_create_typed_table_complex_combinations OF part8_people_type(
    weight WITH OPTIONS NOT NULL,
    name UNIQUE,
    PRIMARY KEY (id)
)
USING heap
WITH (vacuum_index_cleanup = ON, autovacuum_vacuum_scale_factor = 0.2, vacuum_truncate = true)
TABLESPACE pg_default;

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

-- TOFIX
-- CREATE TABLE part9_partition_with_options_constraintscd
-- PARTITION OF part9_parent_table_range (
--     id PRIMARY KEY,
--     name WITH OPTIONS NOT NULL,
--     weight,
--     CHECK (height > 0)
-- )
-- FOR VALUES FROM (MINVALUE) TO (2);

-- TOFIX
-- CREATE TABLE part9_partition_with_options_constraints_default
-- PARTITION OF part9_parent_table_range (
--     id PRIMARY KEY,
--     name WITH OPTIONS NOT NULL,
--     CHECK (height > 0)
-- ) DEFAULT;

-- TOFIX
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

-- copied from old create_table.sql
-- Test TableLikeClause is handled properly
CREATE TABLE ctlt1 (a text CHECK (length(a) > 2) PRIMARY KEY, b text);
ALTER TABLE ctlt1 ALTER COLUMN a SET STORAGE MAIN;
ALTER TABLE ctlt1 ALTER COLUMN b SET STORAGE EXTERNAL;
CREATE TABLE ctlt1_like (LIKE ctlt1 INCLUDING ALL);

-- Test foreign key constraint is handled in a following ALTER TABLE ADD CONSTRAINT FOREIGN KEY REFERENCES subcommand
CREATE TABLE product (id int PRIMARY KEY, name text);
CREATE TABLE orders2 (order_id int PRIMARY KEY, product_id int
REFERENCES product (id));

-- Test CREATE and ALTER inherited table
CREATE TABLE gtest30 (
a int,
b int GENERATED ALWAYS AS (a * 2) STORED
);
CREATE TABLE gtest30_1 () INHERITS (gtest30);
ALTER TABLE gtest30 ALTER COLUMN b DROP EXPRESSION;
