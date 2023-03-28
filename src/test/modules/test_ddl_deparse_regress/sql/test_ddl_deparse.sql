CREATE ROLE ddl_testing_role SUPERUSER;
SET ROLE ddl_testing_role;
SET allow_in_place_tablespaces = true;
CREATE TABLESPACE ddl_testing_tablespace LOCATION '';
CREATE TABLESPACE ddl_testing_tablespace_backup LOCATION '';
CREATE EXTENSION test_ddl_deparse_regress;

CREATE OR REPLACE FUNCTION test_ddl_deparse()
  RETURNS event_trigger LANGUAGE plpgsql AS
$$
DECLARE
	r record;
	deparsed_json text;
BEGIN
	FOR r IN SELECT * FROM pg_event_trigger_ddl_commands()
	LOOP
		deparsed_json = pg_catalog.ddl_deparse_to_json(r.command);
		RAISE NOTICE 'deparsed json: %', deparsed_json;
		RAISE NOTICE 're-formed command: %', pg_catalog.ddl_deparse_expand_command(deparsed_json);
	END LOOP;
END;
$$;

CREATE EVENT TRIGGER test_ddl_deparse
ON ddl_command_end EXECUTE PROCEDURE test_ddl_deparse();

CREATE OR REPLACE FUNCTION test_deparse_create_table_as()
	RETURNS event_trigger LANGUAGE plpgsql AS
$$
DECLARE
	deparsed_json text;
BEGIN
	deparsed_json = deparse_table_init_write();
	RAISE NOTICE 'deparsed json: %', deparsed_json;
	RAISE NOTICE 're-formed command: %', pg_catalog.ddl_deparse_expand_command(deparsed_json);
END;
$$;

CREATE EVENT TRIGGER test_deparse_create_table_as
ON table_init_write EXECUTE PROCEDURE test_deparse_create_table_as();
