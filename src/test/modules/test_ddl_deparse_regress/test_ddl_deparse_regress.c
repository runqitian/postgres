/*----------------------------------------------------------------------
 * test_ddl_deparse_regress.c
 *		Support functions for the test_ddl_deparse_regress module
 *
 * Copyright (c) 2014-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/test/modules/test_ddl_deparse_regress/test_ddl_deparse_regress.c
 *----------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"
#include "catalog/pg_class.h"
#include "funcapi.h"
#include "nodes/execnodes.h"
#include "tcop/deparse_utility.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "tcop/ddl_deparse.h"
#include "commands/event_trigger.h"

extern EventTriggerQueryState *currentEventTriggerState;

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(deparse_drop_ddl);
PG_FUNCTION_INFO_V1(deparse_table_init_write);
PG_FUNCTION_INFO_V1(publish_deparse_table_init_write);

/*
 * Given object_identity and object_type of dropped object, return a JSON representation of DROP command.
 */
Datum
deparse_drop_ddl(PG_FUNCTION_ARGS)
{
	text	   *objidentity = PG_GETARG_TEXT_P(0);
	const char	   *objidentity_str = text_to_cstring(objidentity);
	text	   *objecttype = PG_GETARG_TEXT_P(1);
	const char	   *objecttype_str = text_to_cstring(objecttype);

	char		   *command;

	// constraint is part of alter table command, no need to drop in DROP command
	if (strcmp(objecttype_str, "table constraint") == 0) {
		PG_RETURN_NULL();
	} else if (strcmp(objecttype_str, "toast table") == 0) {
		objecttype_str = "table";
	}  else if (strcmp(objecttype_str, "default value") == 0) {
		PG_RETURN_NULL();
	} else if (strcmp(objecttype_str, "operator of access method") == 0) {
		PG_RETURN_NULL();
	} else if (strcmp(objecttype_str, "function of access method") == 0) {
		PG_RETURN_NULL();
	} else if (strcmp(objecttype_str, "table column") == 0) {
		PG_RETURN_NULL();
	}

	command = deparse_drop_command(objidentity_str, objecttype_str, DROP_CASCADE);

	if (command)
		PG_RETURN_TEXT_P(cstring_to_text(command));

	PG_RETURN_NULL();
}

/*
 * deparse_table_init_write
 *
 * Deparse the ddl table create command and return it.
 */
Datum
deparse_table_init_write(PG_FUNCTION_ARGS)
{
	char		relpersist;
	CollectedCommand *cmd;
	char	   *json_string;

	cmd = currentEventTriggerState->currentCommand;
	Assert(cmd);

	relpersist = get_rel_persistence(cmd->d.simple.address.objectId);

	/*
	 * Do not generate wal log for commands whose target table is a temporary
	 * table.
	 *
	 * We will generate wal logs for unlogged tables so that unlogged tables
	 * can also be created and altered on the subscriber side. This makes it
	 * possible to directly replay the SET LOGGED command and the incoming
	 * rewrite message without creating a new table.
	 */
	if (relpersist == RELPERSISTENCE_TEMP)
		PG_RETURN_NULL();

	/* Deparse the DDL command and WAL log it to allow decoding of the same. */
	json_string = deparse_utility_command(cmd, false, true);
	
	if (json_string != NULL)
	{
		PG_RETURN_TEXT_P(cstring_to_text(json_string));
	}

	PG_RETURN_NULL();
}
