CREATE TABLE test_index_base(
    id1 int,
    id2 int,
    id3 int,
    id4 int,
    id5 int,
    id6 int,
    id7 int,
    id8 int,
    position point,
    name varchar,
    description text
);

-- CREATE [ UNIQUE ] INDEX [ CONCURRENTLY ] [ [ IF NOT EXISTS ] name ] ON [ ONLY ] table_name [ USING method ]
CREATE TABLE test_create_index_prefix(
    LIKE test_index_base
);
CREATE TABLE test_create_index_prefix_partitioned(
    LIKE test_index_base
) PARTITION BY RANGE (id8);
CREATE TABLE test_create_index_prefix_partitioned_part1 PARTITION OF test_create_index_prefix_partitioned DEFAULT;

CREATE INDEX ON test_create_index_prefix (id1);
CREATE UNIQUE INDEX ON test_create_index_prefix (id2);
CREATE INDEX CONCURRENTLY ON test_create_index_prefix(id3);
CREATE INDEX customized_index_on_id4 ON test_create_index_prefix (id4);
CREATE INDEX IF NOT EXISTS customized_index_on_id4 ON test_create_index_prefix (id4);
CREATE INDEX ON ONLY test_create_index_prefix_partitioned (id1);
CREATE INDEX ON test_create_index_prefix_partitioned (id2);
CREATE INDEX ON test_create_index_prefix USING gist (position);
CREATE INDEX CONCURRENTLY IF NOT EXISTS customized_index_on_id5 ON test_create_index_prefix USING hash (id5);
CREATE UNIQUE INDEX CONCURRENTLY IF NOT EXISTS customized_index_on_id6 ON test_create_index_prefix USING btree (id6);
CREATE UNIQUE INDEX IF NOT EXISTS customized_index_on_id8 ON ONLY test_create_index_prefix_partitioned USING btree (id8);

-- ( { column_name | ( expression ) } [ COLLATE collation ] [ opclass [ ( opclass_parameter = value [, ... ] ) ] ] [ ASC | DESC ] [ NULLS { FIRST | LAST } ] [, ...] )
CREATE TABLE test_create_index_definition(
    LIKE test_index_base
);
CREATE INDEX ON test_create_index_definition (
    id1, id2
);
CREATE INDEX ON test_create_index_definition(
    (id1 + id2)
);
CREATE INDEX ON test_create_index_definition (
    name COLLATE "fr_FR"
);
CREATE INDEX ON test_create_index_definition USING brin (
    id3 int4_minmax_multi_ops
);
CREATE INDEX ON test_create_index_definition (
    id4 ASC
);
CREATE INDEX ON test_create_index_definition (
    id5 DESC
);
CREATE INDEX ON test_create_index_definition (
    id6 DESC NULLS LAST
);
CREATE INDEX ON test_create_index_definition (
    id7 NULLS FIRST
);
CREATE INDEX ON test_create_index_definition (
    description COLLATE "fr_FR" DESC NULLS LAST,
    (id3 * id4) int4_ops ASC NULLS FIRST
);

-- [ INCLUDE ( column_name [, ...] ) ]
CREATE TABLE test_create_index_suffix(
    LIKE test_index_base
);
CREATE INDEX ON test_create_index_suffix (id1)
    INCLUDE (name, position);
CREATE INDEX ON test_create_index_suffix (id2)
    INCLUDE (name);
-- [ NULLS [ NOT ] DISTINCT ]
CREATE INDEX ON test_create_index_suffix (id3)
    NULLS DISTINCT;
CREATE INDEX ON test_create_index_suffix (id4)
    NULLS NOT DISTINCT;

-- TOFIX:  NULLS DISTINCT option should not be placed at the end of reformed statement
-- [ WITH ( storage_parameter [= value] [, ... ] ) ]
-- CREATE INDEX ON test_create_index_suffix (id5)
--     WITH (fillfactor = 20);
-- CREATE INDEX ON test_create_index_suffix USING brin (id6)
--     WITH (pages_per_range = 10, autosummarize = ON);
-- [ TABLESPACE tablespace_name ]
-- CREATE INDEX ON test_create_index_definition ((id1 + id2))
--     TABLESPACE ddl_testing_tablespace;
-- [ WHERE predicate ]
-- CREATE INDEX ON test_create_index_definition ((id2 + id3))
--     WHERE (id2 > 200 AND id3 < 500);
-- CREATE INDEX ON test_create_index_definition ((id3 + id4))
--     INCLUDE (id7, name)
--     NULLS NOT DISTINCT
--     WITH (fillfactor = 20, deduplicate_items=OFF)
--     TABLESPACE ddl_testing_tablespace
--     WHERE (id4 > 100);

-- complex statements
-- TODO after above fix


-- ALTER INDEX
CREATE TABLE test_alter_index(
    LIKE test_index_base
);
CREATE INDEX test_alter_index_idx1 ON test_alter_index (id1);
CREATE INDEX test_alter_index_idx2 ON test_alter_index (id2);
CREATE INDEX test_alter_index_idx3 ON test_alter_index (id3);
CREATE INDEX test_alter_index_idx4 ON test_alter_index (id4);
CREATE INDEX test_alter_index_idx5 ON test_alter_index (id5);
CREATE INDEX test_alter_index_idx6 ON test_alter_index (id6);
CREATE INDEX test_alter_index_idx7 ON test_alter_index (id7);
CREATE INDEX test_alter_index_idx8 ON test_alter_index (id8);
CREATE TABLE test_alter_index_partitioned(
    LIKE test_index_base
) PARTITION BY RANGE (id8);
CREATE TABLE test_alter_index_partitioned_part1
    PARTITION OF test_alter_index_partitioned DEFAULT;
CREATE INDEX test_alter_index_partitioned_idx ON ONLY test_alter_index_partitioned (id1);
CREATE INDEX CONCURRENTLY test_alter_index_partitioned_part1_idx ON test_alter_index_partitioned_part1 (id1);

-- ALTER INDEX [ IF EXISTS ] name RENAME TO new_name
ALTER INDEX test_alter_index_idx1 RENAME TO test_alter_index_idx1_new_idx;
ALTER INDEX IF EXISTS test_alter_index_fake_index RENAME TO test_alter_index_fake_new_idx;

-- ALTER INDEX [ IF EXISTS ] name SET TABLESPACE tablespace_name
ALTER INDEX test_alter_index_idx2 SET TABLESPACE ddl_testing_tablespace;
ALTER INDEX IF EXISTS test_alter_index_fake_index SET TABLESPACE ddl_testing_tablespace;

-- ALTER INDEX name ATTACH PARTITION index_name
-- TOFIX
-- ALTER INDEX test_alter_index_partitioned_idx ATTACH PARTITION test_alter_index_partitioned_part1_idx;

-- ALTER INDEX name [ NO ] DEPENDS ON EXTENSION extension_name
CREATE EXTENSION pg_stat_statements;
ALTER INDEX test_alter_index_idx3 DEPENDS ON EXTENSION pg_stat_statements;
DROP EXTENSION pg_stat_statements;
CREATE EXTENSION pg_stat_statements;
ALTER INDEX test_alter_index_idx4 DEPENDS ON EXTENSION pg_stat_statements;
ALTER INDEX test_alter_index_idx4 NO DEPENDS ON EXTENSION pg_stat_statements;
DROP EXTENSION pg_stat_statements;

-- ALTER INDEX [ IF EXISTS ] name SET ( storage_parameter [= value] [, ... ] )
ALTER INDEX test_alter_index_idx5 SET (fillfactor = 30);
ALTER INDEX test_alter_index_idx6 SET (fillfactor = 30, deduplicate_items=OFF);
ALTER INDEX IF EXISTS test_alter_index_fake_index SET (fillfactor = 30, deduplicate_items=OFF);

-- ALTER INDEX [ IF EXISTS ] name RESET ( storage_parameter [, ... ] )
ALTER INDEX test_alter_index_idx7 SET (fillfactor = 30);
ALTER INDEX test_alter_index_idx7 RESET (fillfactor);
ALTER INDEX test_alter_index_idx8 SET (fillfactor = 30, deduplicate_items=OFF);
ALTER INDEX test_alter_index_idx8 RESET (fillfactor, deduplicate_items);
ALTER INDEX IF EXISTS test_alter_index_fake_index RESET (fillfactor, deduplicate_items);

-- ALTER INDEX [ IF EXISTS ] name ALTER [ COLUMN ] column_number
--     SET STATISTICS integer
CREATE INDEX test_alter_index_idx9 ON test_alter_index ((id1 + id2), (id1 * id2));
ALTER INDEX test_alter_index_idx9 ALTER 1 SET STATISTICS 9999;
ALTER INDEX test_alter_index_idx9 ALTER COLUMN 2 SET STATISTICS 9999;
ALTER INDEX IF EXISTS test_alter_index_fake_index ALTER COLUMN 2 SET STATISTICS 9999;

-- ALTER INDEX ALL IN TABLESPACE name [ OWNED BY role_name [, ... ] ]
--     SET TABLESPACE new_tablespace [ NOWAIT ]
-- TOFIX
-- CREATE INDEX test_alter_index_idx10 ON test_alter_index ((id2 + id3))
--     TABLESPACE ddl_testing_tablespace_backup;
-- ALTER INDEX ALL IN TABLESPACE ddl_testing_tablespace_backup SET TABLESPACE ddl_testing_tablespace;
-- CREATE INDEX test_alter_index_idx11 ON test_alter_index ((id3 + id4))
--     TABLESPACE ddl_testing_tablespace_backup;
-- ALTER INDEX ALL IN TABLESPACE ddl_testing_tablespace_backup SET TABLESPACE ddl_testing_tablespace NOWAIT;
-- TODO: OWNED BY

