CREATE TABLE test_sequence_base(
    id1 int,
    id2 int,
    id3 int,
    id4 int,
    id5 int,
    id6 int
);
CREATE SCHEMA test_alter_sequence_schema;

-- CREATE SEQUENCE
-- CREATE [ { TEMPORARY | TEMP } | UNLOGGED ] SEQUENCE [ IF NOT EXISTS ] name
CREATE SEQUENCE test_sequence_simple;
CREATE TEMP SEQUENCE test_sequence_temp;
CREATE TEMPORARY SEQUENCE test_sequence_temp2;
CREATE UNLOGGED SEQUENCE test_sequence_unlogged;
CREATE SEQUENCE IF NOT EXISTS test_sequence_simple;

-- [ AS data_type ]
CREATE SEQUENCE test_sequence_data_type_smallint AS smallint;
CREATE SEQUENCE test_sequence_data_type_integer AS integer;
CREATE SEQUENCE test_sequence_data_type_bigint AS bigint;

-- [ INCREMENT [ BY ] increment ]
CREATE SEQUENCE test_sequence_increment INCREMENT 10;
CREATE SEQUENCE test_sequence_increment_by INCREMENT 101;

-- [ MINVALUE minvalue | NO MINVALUE ] [ MAXVALUE maxvalue | NO MAXVALUE ]
CREATE SEQUENCE test_sequence_minvalue MINVALUE 99;
CREATE SEQUENCE test_sequence_no_minvalue NO MINVALUE;
CREATE SEQUENCE test_sequence_maxvalue MAXVALUE 999;
CREATE SEQUENCE test_sequence_no_maxvalue NO MAXVALUE;
CREATE SEQUENCE test_sequence_min_and_max1 MINVALUE 99 MAXVALUE 777;
CREATE SEQUENCE test_sequence_min_and_max2 NO MINVALUE MAXVALUE 777;
CREATE SEQUENCE test_sequence_min_and_max3 MINVALUE 99 NO MAXVALUE;
CREATE SEQUENCE test_sequence_min_and_max4 NO MINVALUE NO MAXVALUE;

-- [ START [ WITH ] start ] [ CACHE cache ] [ [ NO ] CYCLE ]
CREATE SEQUENCE test_sequence_start START 101;
CREATE SEQUENCE test_sequence_start_with START WITH 101;
CREATE SEQUENCE test_sequence_cache CACHE 100;
CREATE SEQUENCE test_sequence_cycle CYCLE;
CREATE SEQUENCE test_sequence_no_cycle NO CYCLE;
CREATE SEQUENCE test_sequence_start_cache_cycle1 START WITH 201 CACHE 50 CYCLE;
CREATE SEQUENCE test_sequence_start_cache_cycle2 START 301 CACHE 10 NO CYCLE;
CREATE SEQUENCE test_sequence_start_cache_cycle3 CACHE 10 NO CYCLE;

-- [ OWNED BY { table_name.column_name | NONE } ]
CREATE SEQUENCE test_sequence_owned_by_none OWNED BY NONE;
-- TOFIX
-- CREATE SEQUENCE test_sequence_owned_by OWNED BY test_sequence_base.id1;

-- complex commands
-- TOFIX
-- CREATE SEQUENCE test_sequence_complex1
--     AS smallint
--     INCREMENT BY 40
--     MINVALUE 99 MAXVALUE 9999
--     START WITH 101 CACHE 50 CYCLE
--     OWNED BY test_sequence_base.id2;

-- CREATE SEQUENCE test_sequence_complex2
--     AS integer
--     NO MINVALUE
--     CACHE 10 NO CYCLE
--     OWNED BY test_sequence_base.id2;

CREATE SEQUENCE test_sequence_complex3
    INCREMENT 40
    NO MAXVALUE
    START WITH 20 NO CYCLE
    OWNED BY NONE;


-- ALTER SEQUENCE
-- ALTER SEQUENCE [ IF EXISTS ] name
--     [ AS data_type ]
ALTER SEQUENCE IF EXISTS test_alter_fake_sequence AS smallint;
CREATE SEQUENCE test_alter_sequence_as_data_type;
ALTER SEQUENCE test_alter_sequence_as_data_type AS smallint;

-- [ INCREMENT [ BY ] increment ]
CREATE SEQUENCE test_alter_sequence_increment;
ALTER SEQUENCE test_alter_sequence_increment INCREMENT 10;
CREATE SEQUENCE test_alter_sequence_increment_by;
ALTER SEQUENCE test_alter_sequence_increment_by INCREMENT BY 11;

-- [ MINVALUE minvalue | NO MINVALUE ] [ MAXVALUE maxvalue | NO MAXVALUE ]
CREATE SEQUENCE test_alter_sequence_minvalue;
ALTER SEQUENCE test_alter_sequence_minvalue MINVALUE 100 START 101 RESTART 102;
CREATE SEQUENCE test_alter_sequence_no_minvalue MINVALUE 1000;
ALTER SEQUENCE test_alter_sequence_no_minvalue NO MINVALUE;
CREATE SEQUENCE test_alter_sequence_maxvalue;
ALTER SEQUENCE test_alter_sequence_maxvalue MAXVALUE 100;
CREATE SEQUENCE test_alter_sequence_no_maxvalue MAXVALUE 1000;
ALTER SEQUENCE test_alter_sequence_no_maxvalue NO MAXVALUE;

-- [ START [ WITH ] start ]
CREATE SEQUENCE test_alter_sequence_start;
ALTER SEQUENCE test_alter_sequence_start START WITH 99;
CREATE SEQUENCE test_alter_sequence_start_with;
ALTER SEQUENCE test_alter_sequence_start_with START WITH 99;

-- [ RESTART [ [ WITH ] restart ] ]
CREATE SEQUENCE test_alter_sequence_restart;
ALTER SEQUENCE test_alter_sequence_restart RESTART;
CREATE SEQUENCE test_alter_sequence_restart_value;
ALTER SEQUENCE test_alter_sequence_restart_value RESTART 99;
CREATE SEQUENCE test_alter_sequence_restart_with_value;
ALTER SEQUENCE test_alter_sequence_restart_with_value RESTART WITH 99;

-- [ CACHE cache ] [ [ NO ] CYCLE ]
CREATE SEQUENCE test_alter_sequence_cache;
ALTER SEQUENCE test_alter_sequence_cache CACHE 99;
CREATE SEQUENCE test_alter_sequence_cycle;
ALTER SEQUENCE test_alter_sequence_cycle CYCLE;
CREATE SEQUENCE test_alter_sequence_no_cycle CYCLE;
ALTER SEQUENCE test_alter_sequence_no_cycle NO CYCLE;
CREATE SEQUENCE test_alter_sequence_cache_cycle;
ALTER SEQUENCE test_alter_sequence_cache_cycle CACHE 99 CYCLE;

-- [ OWNED BY { table_name.column_name | NONE } ]
CREATE SEQUENCE test_alter_sequence_owned_by_none OWNED BY test_sequence_base.id3;
-- TOFIX
-- ALTER SEQUENCE test_alter_sequence_owned_by_none OWNED BY NONE;
CREATE SEQUENCE test_alter_sequence_owned_by;
ALTER SEQUENCE test_alter_sequence_owned_by_none OWNED BY test_sequence_base.id4;

-- complex commands
CREATE SEQUENCE test_alter_sequence_complex1;
ALTER SEQUENCE test_alter_sequence_complex1 
    AS smallint 
    INCREMENT BY 99 
    MINVALUE 9 MAXVALUE 200 
    START WITH 11
    RESTART WITH 12
    CACHE 15 CYCLE
    OWNED BY test_sequence_base.id5;
CREATE SEQUENCE test_alter_sequence_complex2 CYCLE MINVALUE 100 MAXVALUE 888 OWNED BY test_sequence_base.id6;
ALTER SEQUENCE test_alter_sequence_complex2 
    AS smallint 
    INCREMENT BY 99 
    NO MINVALUE NO MAXVALUE
    RESTART WITH 8
    CACHE 15 NO CYCLE
    OWNED BY NONE;

-- ALTER SEQUENCE [ IF EXISTS ] name SET { LOGGED | UNLOGGED }
ALTER SEQUENCE IF EXISTS test_alter_fake_sequence SET UNLOGGED;
-- TOFIX
-- CREATE UNLOGGED SEQUENCE test_alter_sequence_logged;
-- ALTER SEQUENCE test_alter_sequence_logged SET LOGGED;
-- CREATE SEQUENCE test_alter_sequence_unlogged;
-- ALTER SEQUENCE test_alter_sequence_unlogged SET UNLOGGED;

-- ALTER SEQUENCE [ IF EXISTS ] name OWNER TO { new_owner | CURRENT_ROLE | CURRENT_USER | SESSION_USER }
ALTER SEQUENCE IF EXISTS test_alter_fake_sequence OWNER TO CURRENT_USER;
-- TODO

-- ALTER SEQUENCE [ IF EXISTS ] name RENAME TO new_name
ALTER SEQUENCE IF EXISTS test_alter_fake_sequence RENAME TO test_alter_fake_sequence_new;
CREATE SEQUENCE test_alter_sequence_rename_old;
ALTER SEQUENCE test_alter_sequence_rename_old RENAME TO test_alter_sequence_rename_new;

-- ALTER SEQUENCE [ IF EXISTS ] name SET SCHEMA new_schema
ALTER SEQUENCE IF EXISTS test_alter_fake_sequence SET SCHEMA test_alter_sequence_schema;
CREATE SEQUENCE test_alter_sequence_schema;
ALTER SEQUENCE test_alter_sequence_schema  SET SCHEMA test_alter_sequence_schema;