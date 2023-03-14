/*-------------------------------------------------------------------------
 *
 * ddl_deparse.c
 *	  Functions to convert utility commands to machine-parseable
 *	  representation
 *
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * NOTES
 *
 * This is intended to provide JSON blobs representing DDL commands, which can
 * later be re-processed into plain strings by well-defined sprintf-like
 * expansion.  These JSON objects are intended to allow for machine-editing of
 * the commands, by replacing certain nodes within the objects.
 *
 * Much of the information in the output blob actually comes from system
 * catalogs, not from the command parse node, as it is impossible to reliably
 * construct a fully-specified command (i.e. one not dependent on search_path
 * etc) looking only at the parse node.
 *
 * Deparse object tree is created by using:
 * 	a) new_objtree("know contents") where the complete tree content is known or
 *     the initial tree content is known.
 * 	b) new_objtree("") for the syntax where the object tree will be derived
 *     based on some conditional checks.
 * 	c) new_objtree_VA where the complete tree can be derived using some fixed
 *     content and/or some variable arguments.
 *
 * IDENTIFICATION
 *	  src/backend/commands/ddl_deparse.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/amapi.h"
#include "access/relation.h"
#include "access/table.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_cast.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_conversion.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_language.h"
#include "catalog/pg_largeobject.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_range.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_sequence.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_transform.h"
#include "catalog/pg_ts_config.h"
#include "catalog/pg_ts_dict.h"
#include "catalog/pg_ts_parser.h"
#include "catalog/pg_ts_template.h"
#include "catalog/pg_type.h"
#include "catalog/pg_user_mapping.h"
#include "commands/defrem.h"
#include "commands/sequence.h"
#include "commands/tablespace.h"
#include "foreign/foreign.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "nodes/nodeFuncs.h"
#include "nodes/parsenodes.h"
#include "optimizer/optimizer.h"
#include "parser/parse_type.h"
#include "rewrite/rewriteHandler.h"
#include "tcop/ddl_deparse.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/ruleutils.h"
#include "utils/syscache.h"

/* Estimated length of the generated jsonb string */
#define JSONB_ESTIMATED_LEN 128

/*
 * Before they are turned into JSONB representation, each command is
 * represented as an object tree, using the structs below.
 */
typedef enum
{
	ObjTypeNull,
	ObjTypeBool,
	ObjTypeString,
	ObjTypeArray,
	ObjTypeInteger,
	ObjTypeFloat,
	ObjTypeObject
} ObjType;

/*
 * Represent the command as an object tree.
 */
typedef struct ObjTree
{
	slist_head	params;			/* Object tree parameters */
	int			numParams;		/* Number of parameters in the object tree */
	StringInfo	fmtinfo;		/* Format string of the ObjTree */
	bool		present;		/* Indicates if boolean value should be stored */
} ObjTree;

/*
 * An element of an object tree (ObjTree).
 */
typedef struct ObjElem
{
	char	   *name;			/* Name of object element */
	ObjType		objtype;		/* Object type */

	union
	{
		bool		boolean;
		char	   *string;
		int64		integer;
		float8		flt;
		ObjTree    *object;
		List	   *array;
	}			value;			/* Store the object value based on the object
								 * type */
	slist_node	node;			/* Used in converting back to ObjElem
								 * structure */
} ObjElem;

/*
 * Reduce some unnecessary strings from the output json when verbose
 * and "present" member is false. This means these strings won't be merged into
 * the last DDL command.
 */
bool		verbose = true;

static void append_array_object(ObjTree *tree, char *sub_fmt, List *array);
static void append_bool_object(ObjTree *tree, char *sub_fmt, bool value);
static void append_null_object(ObjTree *tree, char *sub_fmt);
static void append_object_object(ObjTree *tree, char *sub_fmt, ObjTree *value);
static char *append_object_to_format_string(ObjTree *tree, char *sub_fmt);
static void append_premade_object(ObjTree *tree, ObjElem *elem);
static void append_string_object(ObjTree *tree, char *sub_fmt, char *name,
								 char *value);
static void format_type_detailed(Oid type_oid, int32 typemod,
								 Oid *nspid, char **typname, char **typemodstr,
								 bool *typarray);
static ObjElem *new_object(ObjType type, char *name);
static ObjTree *new_objtree_for_qualname_id(Oid classId, Oid objectId);
static ObjElem *new_object_object(ObjTree *value);
static ObjTree *new_objtree_VA(char *fmt, int numobjs,...);
static JsonbValue *objtree_to_jsonb_rec(ObjTree *tree, JsonbParseState *state);
static void pg_get_indexdef_detailed(Oid indexrelid,
									 char **index_am,
									 char **definition,
									 char **reloptions,
									 char **tablespace,
									 char **whereClause);
static char *RelationGetColumnDefault(Relation rel, AttrNumber attno,
									  List *dpcontext, List **exprs);

static ObjTree *deparse_ColumnDef(Relation relation, List *dpcontext, bool composite,
								  ColumnDef *coldef, bool is_alter, List **exprs);
static ObjTree *deparse_ColumnIdentity(Oid seqrelid, char identity, bool alter_table);
static ObjTree *deparse_ColumnSetOptions(AlterTableCmd *subcmd);

static ObjTree *deparse_DefElem(DefElem *elem, bool is_reset);
static ObjTree *deparse_OnCommitClause(OnCommitAction option);
static ObjTree *deparse_RelSetOptions(AlterTableCmd *subcmd);

static inline ObjElem *deparse_Seq_Cache(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_Cycle(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_IncrementBy(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_Minvalue(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_Maxvalue(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_Restart(int64 last_value);
static inline ObjElem *deparse_Seq_Startwith(Form_pg_sequence seqdata, bool alter_table);
static inline ObjElem *deparse_Seq_As(Form_pg_sequence seqdata);
static inline ObjElem *deparse_Type_Storage(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Receive(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Send(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Typmod_In(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Typmod_Out(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Analyze(Form_pg_type typForm);
static inline ObjElem *deparse_Type_Subscript(Form_pg_type typForm);

static List *deparse_InhRelations(Oid objectId);
static List *deparse_TableElements(Relation relation, List *tableElements, List *dpcontext,
								   bool typed, bool composite);

/*
 * Append present as false to a tree.
 */
static void
append_not_present(ObjTree *tree)
{
	append_bool_object(tree, "present", false);
}

/*
 * Append an array parameter to a tree.
 */
static void
append_array_object(ObjTree *tree, char *sub_fmt, List *array)
{
	ObjElem    *param;
	char	   *object_name;

	Assert(sub_fmt);

	if (list_length(array) == 0)
		return;

	if (!verbose)
	{
		ListCell   *lc;

		/* Remove elements where present flag is false */
		foreach(lc, array)
		{
			ObjElem    *elem = (ObjElem *) lfirst(lc);

			Assert(elem->objtype == ObjTypeObject ||
				   elem->objtype == ObjTypeString);

			if (!elem->value.object->present &&
				elem->objtype == ObjTypeObject)
				array = foreach_delete_current(array, lc);
		}

	}

	/* Check for empty list after removing elements */
	if (list_length(array) == 0)
		return;

	object_name = append_object_to_format_string(tree, sub_fmt);

	param = new_object(ObjTypeArray, object_name);
	param->value.array = array;
	append_premade_object(tree, param);
}

/*
 * Append a boolean parameter to a tree.
 */
static void
append_bool_object(ObjTree *tree, char *sub_fmt, bool value)
{
	ObjElem    *param;
	char	   *object_name = sub_fmt;
	bool		is_present_flag = false;

	Assert(sub_fmt);

	/*
	 * Check if the format string is 'present' and if yes, store the boolean
	 * value
	 */
	if (strcmp(sub_fmt, "present") == 0)
	{
		is_present_flag = true;
		tree->present = value;
	}

	if (!is_present_flag)
		object_name = append_object_to_format_string(tree, sub_fmt);

	param = new_object(ObjTypeBool, object_name);
	param->value.boolean = value;
	append_premade_object(tree, param);
}

/*
 * Append the input format string to the ObjTree.
 */
static void
append_format_string(ObjTree *tree, char *sub_fmt)
{
	int			len;
	char	   *fmt;

	if (tree->fmtinfo == NULL)
		return;

	fmt = tree->fmtinfo->data;
	len = tree->fmtinfo->len;

	/* Add a separator if necessary */
	if (len > 0 && fmt[len - 1] != ' ')
		appendStringInfoSpaces(tree->fmtinfo, 1);

	appendStringInfoString(tree->fmtinfo, sub_fmt);
}

/*
 * Append a NULL object to a tree.
 */
static void
append_null_object(ObjTree *tree, char *sub_fmt)
{
	char	   *object_name;

	Assert(sub_fmt);

	if (!verbose)
		return;

	object_name = append_object_to_format_string(tree, sub_fmt);

	append_premade_object(tree, new_object(ObjTypeNull, object_name));
}

/*
 * Append an object parameter to a tree.
 */
static void
append_object_object(ObjTree *tree, char *sub_fmt, ObjTree *value)
{
	ObjElem    *param;
	char	   *object_name;

	Assert(sub_fmt);

	if (!verbose && !value->present)
		return;

	object_name = append_object_to_format_string(tree, sub_fmt);

	param = new_object(ObjTypeObject, object_name);
	param->value.object = value;
	append_premade_object(tree, param);
}

/*
 * Return the object name which is extracted from the input "*%{name[:.]}*"
 * style string. And append the input format string to the ObjTree.
 */
static char *
append_object_to_format_string(ObjTree *tree, char *sub_fmt)
{
	StringInfoData object_name;
	const char *end_ptr, *start_ptr;
	int         length;
	char        *tmp_str;

	if (sub_fmt == NULL || tree->fmtinfo == NULL)
		return sub_fmt;

	initStringInfo(&object_name);

	start_ptr = strchr(sub_fmt, '{');
	end_ptr = strchr(sub_fmt, ':');
	if (end_ptr == NULL)
		end_ptr = strchr(sub_fmt, '}');

	if (start_ptr != NULL && end_ptr != NULL)
	{
		length = end_ptr - start_ptr - 1;
		tmp_str = (char *) palloc(length + 1);
		strncpy(tmp_str, start_ptr + 1, length);
		tmp_str[length] = '\0';
		appendStringInfoString(&object_name, tmp_str);
		pfree(tmp_str);
	}

	if (object_name.len == 0)
		elog(ERROR, "object name not found");

	append_format_string(tree, sub_fmt);

	return object_name.data;

}

/*
 * Append a preallocated parameter to a tree.
 */
static inline void
append_premade_object(ObjTree *tree, ObjElem *elem)
{
	slist_push_head(&tree->params, &elem->node);
	tree->numParams++;
}

/*
 * Append a string parameter to a tree.
 */
static void
append_string_object(ObjTree *tree, char *sub_fmt, char * object_name,
					 char *value)
{
	ObjElem    *param;

	Assert(sub_fmt);

	if (!verbose && (value == NULL || value[0] == '\0'))
		return;

	append_format_string(tree, sub_fmt);
	param = new_object(ObjTypeString, object_name);
	param->value.string = value;
	append_premade_object(tree, param);
}

/*
 * Similar to format_type_extended, except we return each bit of information
 * separately:
 *
 * - nspid is the schema OID.  For certain SQL-standard types which have weird
 *   typmod rules, we return InvalidOid; the caller is expected to not schema-
 *   qualify the name nor add quotes to the type name in this case.
 *
 * - typname is set to the type name, without quotes
 *
 * - typemodstr is set to the typemod, if any, as a string with parentheses
 *
 * - typarray indicates whether []s must be added
 *
 * We don't try to decode type names to their standard-mandated names, except
 * in the cases of types with unusual typmod rules.
 */
static void
format_type_detailed(Oid type_oid, int32 typemod,
					 Oid *nspid, char **typename, char **typemodstr,
					 bool *typearray)
{
	HeapTuple	tuple;
	Form_pg_type typeform;
	Oid			array_base_type;

	tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(type_oid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for type with OID %u", type_oid);

	typeform = (Form_pg_type) GETSTRUCT(tuple);

	/*
	 * Check if it's a regular (variable length) array type.  As above,
	 * fixed-length array types such as "name" shouldn't get deconstructed.
	 */
	array_base_type = typeform->typelem;

	*typearray = (IsTrueArrayType(typeform) && typeform->typstorage != TYPSTORAGE_PLAIN);

	if (*typearray)
	{
		/* Switch our attention to the array element type */
		ReleaseSysCache(tuple);
		tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(array_base_type));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for type with OID %u", type_oid);

		typeform = (Form_pg_type) GETSTRUCT(tuple);
		type_oid = array_base_type;
	}

	/*
	 * Special-case crock for types with strange typmod rules where we put
	 * typemod at the middle of name (e.g. TIME(6) with time zone). We cannot
	 * schema-qualify nor add quotes to the type name in these cases.
	 */
	*nspid = InvalidOid;

	switch (type_oid)
	{
		case INTERVALOID:
			*typename = pstrdup("INTERVAL");
			break;
		case TIMESTAMPTZOID:
			if (typemod < 0)
				*typename = pstrdup("TIMESTAMP WITH TIME ZONE");
			else
				/* otherwise, WITH TZ is added by typmod. */
				*typename = pstrdup("TIMESTAMP");
			break;
		case TIMESTAMPOID:
			*typename = pstrdup("TIMESTAMP");
			break;
		case TIMETZOID:
			if (typemod < 0)
				*typename = pstrdup("TIME WITH TIME ZONE");
			else
				/* otherwise, WITH TZ is added by typmod. */
				*typename = pstrdup("TIME");
			break;
		case TIMEOID:
			*typename = pstrdup("TIME");
			break;
		default:

			/*
			 * No additional processing is required for other types, so get
			 * the type name and schema directly from the catalog.
			 */
			*nspid = typeform->typnamespace;
			*typename = pstrdup(NameStr(typeform->typname));
	}

	if (typemod >= 0)
		*typemodstr = printTypmod("", typemod, typeform->typmodout);
	else
		*typemodstr = pstrdup("");

	ReleaseSysCache(tuple);
}

/*
 * Return the string representation of the given RELPERSISTENCE value.
 */
static char *
get_persistence_str(char persistence)
{
	switch (persistence)
	{
		case RELPERSISTENCE_TEMP:
			return "TEMPORARY";
		case RELPERSISTENCE_UNLOGGED:
			return "UNLOGGED";
		case RELPERSISTENCE_PERMANENT:
			return "";
		default:
			elog(ERROR, "unexpected persistence marking %c", persistence);
			return "";			/* make compiler happy */
	}
}

/*
 * Return the string representation of the given storagetype value.
 */
static inline char *
get_type_storage(char storagetype)
{
	switch (storagetype)
	{
		case 'p':
			return "plain";
		case 'e':
			return "external";
		case 'x':
			return "extended";
		case 'm':
			return "main";
		default:
			elog(ERROR, "invalid storage specifier %c", storagetype);
	}
}

/*
 * Allocate a new parameter.
 */
static ObjElem *
new_object(ObjType type, char *name)
{
	ObjElem    *param;

	param = palloc0(sizeof(ObjElem));
	param->name = name;
	param->objtype = type;

	return param;
}

/*
 * Allocate a new object parameter.
 */
static ObjElem *
new_object_object(ObjTree *value)
{
	ObjElem    *param;

	param = new_object(ObjTypeObject, NULL);
	param->value.object = value;

	return param;
}

/*
 * Allocate a new object tree to store parameter values.
 */
static ObjTree *
new_objtree(char *fmt)
{
	ObjTree    *params;

	params = palloc0(sizeof(ObjTree));
	params->present = true;
	slist_init(&params->params);

	if (fmt)
	{
		params->fmtinfo = makeStringInfo();
		appendStringInfoString(params->fmtinfo, fmt);
	}

	return params;
}

/*
 * A helper routine to set up %{}D and %{}O elements.
 *
 * Elements "schema_name" and "obj_name" are set.  If the namespace OID
 * corresponds to a temp schema, that's set to "pg_temp".
 *
 * The difference between those two element types is whether the obj_name will
 * be quoted as an identifier or not, which is not something that this routine
 * concerns itself with; that will be up to the expand function.
 */
static ObjTree *
new_objtree_for_qualname(Oid nspid, char *name)
{
	ObjTree    *qualified;
	char	   *namespace;

	if (isAnyTempNamespace(nspid))
		namespace = pstrdup("pg_temp");
	else
		namespace = get_namespace_name(nspid);

	qualified = new_objtree_VA(NULL, 2,
							   "schemaname", ObjTypeString, namespace,
							   "objname", ObjTypeString, pstrdup(name));

	return qualified;
}

/*
 * A helper routine to set up %{}D and %{}O elements, with the object specified
 * by classId/objId.
 */
static ObjTree *
new_objtree_for_qualname_id(Oid classId, Oid objectId)
{
	ObjTree    *qualified;
	Relation	catalog;
	HeapTuple	catobj;
	Datum		obj_nsp;
	Datum		obj_name;
	AttrNumber	Anum_name;
	AttrNumber	Anum_namespace;
	AttrNumber	Anum_oid = get_object_attnum_oid(classId);
	bool		isnull;

	catalog = table_open(classId, AccessShareLock);

	catobj = get_catalog_object_by_oid(catalog, Anum_oid, objectId);
	if (!catobj)
		elog(ERROR, "cache lookup failed for object with OID %u of catalog \"%s\"",
			 objectId, RelationGetRelationName(catalog));
	Anum_name = get_object_attnum_name(classId);
	Anum_namespace = get_object_attnum_namespace(classId);

	obj_nsp = heap_getattr(catobj, Anum_namespace, RelationGetDescr(catalog),
						  &isnull);
	if (isnull)
		elog(ERROR, "null namespace for object %u", objectId);

	obj_name = heap_getattr(catobj, Anum_name, RelationGetDescr(catalog),
						   &isnull);
	if (isnull)
		elog(ERROR, "null attribute name for object %u", objectId);

	qualified = new_objtree_for_qualname(DatumGetObjectId(obj_nsp),
										 NameStr(*DatumGetName(obj_name)));
	table_close(catalog, AccessShareLock);

	return qualified;
}

/*
 * A helper routine to setup %{}T elements.
 */
static ObjTree *
new_objtree_for_type(Oid typeId, int32 typmod)
{
	Oid			typnspid;
	char	   *type_nsp;
	char	   *type_name = NULL;
	char	   *typmodstr;
	bool		type_array;

	format_type_detailed(typeId, typmod,
						 &typnspid, &type_name, &typmodstr, &type_array);

	if (OidIsValid(typnspid))
		type_nsp = get_namespace_name_or_temp(typnspid);
	else
		type_nsp = pstrdup("");

	return new_objtree_VA(NULL, 4,
						  "schemaname", ObjTypeString, type_nsp,
						  "typename", ObjTypeString, type_name,
						  "typmod", ObjTypeString, typmodstr,
						  "typarray", ObjTypeBool, type_array);
}

/*
 * Allocate a new object tree to store parameter values -- varargs version.
 *
 * The "fmt" argument is used to append as a "fmt" element in the output blob.
 * numobjs indicates the number of extra elements to append; for each one, a
 * name (string), type (from the ObjType enum) and value must be supplied.  The
 * value must match the type given; for instance, ObjTypeInteger requires an
 * int64, ObjTypeString requires a char *, ObjTypeArray requires a list (of
 * ObjElem), ObjTypeObject requires an ObjTree, and so on.  Each element type *
 * must match the conversion specifier given in the format string, as described
 * in ddl_deparse_expand_command, q.v.
 *
 * Note we don't have the luxury of sprintf-like compiler warnings for
 * malformed argument lists.
 */
static ObjTree *
new_objtree_VA(char *fmt, int numobjs,...)
{
	ObjTree    *tree;
	va_list		args;
	int			i;

	/* Set up the toplevel object and its "fmt" */
	tree = new_objtree(fmt);

	/* And process the given varargs */
	va_start(args, numobjs);
	for (i = 0; i < numobjs; i++)
	{
		char	   *name;
		ObjType		type;
		ObjElem    *elem;

		name = va_arg(args, char *);
		type = va_arg(args, ObjType);
		elem = new_object(type, NULL);

		/*
		 * For all param types other than ObjTypeNull, there must be a value in
		 * the varargs. Fetch it and add the fully formed subobject into the
		 * main object.
		 */
		switch (type)
		{
			case ObjTypeNull:
				/* Null params don't have a value (obviously) */
				break;
			case ObjTypeBool:
				elem->value.boolean = va_arg(args, int);
				break;
			case ObjTypeString:
				elem->value.string = va_arg(args, char *);
				break;
			case ObjTypeArray:
				elem->value.array = va_arg(args, List *);
				break;
			case ObjTypeInteger:
				elem->value.integer = va_arg(args, int);
				break;
			case ObjTypeFloat:
				elem->value.flt = va_arg(args, double);
				break;
			case ObjTypeObject:
				elem->value.object = va_arg(args, ObjTree *);
				break;
			default:
				elog(ERROR, "invalid ObjTree element type %d", type);
		}

		elem->name = name;
		append_premade_object(tree, elem);
	}

	va_end(args);
	return tree;
}

/*
 * Process the pre-built format string from the ObjTree into the output parse
 * state.
 */
static void
objtree_fmt_to_jsonb_element(JsonbParseState *state, ObjTree *tree)
{
	JsonbValue	key;
	JsonbValue	val;

	if (tree->fmtinfo == NULL)
		return;

	/* Push the key first */
	key.type = jbvString;
	key.val.string.val = "fmt";
	key.val.string.len = strlen(key.val.string.val);
	pushJsonbValue(&state, WJB_KEY, &key);

	/* Then process the pre-built format string */
	val.type = jbvString;
	val.val.string.len = tree->fmtinfo->len;
	val.val.string.val = tree->fmtinfo->data;
	pushJsonbValue(&state, WJB_VALUE, &val);
}

/*
 * Create a JSONB representation from an ObjTree.
 */
static Jsonb *
objtree_to_jsonb(ObjTree *tree)
{
	JsonbValue *value;

	value = objtree_to_jsonb_rec(tree, NULL);
	return JsonbValueToJsonb(value);
}

/*
 * Helper for objtree_to_jsonb: process an individual element from an object or
 * an array into the output parse state.
 */
static void
objtree_to_jsonb_element(JsonbParseState *state, ObjElem *object,
						 JsonbIteratorToken elem_token)
{
	JsonbValue	val;

	switch (object->objtype)
	{
		case ObjTypeNull:
			val.type = jbvNull;
			pushJsonbValue(&state, elem_token, &val);
			break;

		case ObjTypeString:
			val.type = jbvString;
			val.val.string.len = strlen(object->value.string);
			val.val.string.val = object->value.string;
			pushJsonbValue(&state, elem_token, &val);
			break;

		case ObjTypeInteger:
			val.type = jbvNumeric;
			val.val.numeric = (Numeric)
				DatumGetNumeric(DirectFunctionCall1(int8_numeric,
													object->value.integer));
			pushJsonbValue(&state, elem_token, &val);
			break;

		case ObjTypeFloat:
			val.type = jbvNumeric;
			val.val.numeric = (Numeric)
				DatumGetNumeric(DirectFunctionCall1(float8_numeric,
													object->value.integer));
			pushJsonbValue(&state, elem_token, &val);
			break;

		case ObjTypeBool:
			val.type = jbvBool;
			val.val.boolean = object->value.boolean;
			pushJsonbValue(&state, elem_token, &val);
			break;

		case ObjTypeObject:
			/* Recursively add the object into the existing parse state */
			objtree_to_jsonb_rec(object->value.object, state);
			break;

		case ObjTypeArray:
			{
				ListCell   *cell;

				pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);
				foreach(cell, object->value.array)
				{
					ObjElem    *elem = lfirst(cell);

					objtree_to_jsonb_element(state, elem, WJB_ELEM);
				}
				pushJsonbValue(&state, WJB_END_ARRAY, NULL);
			}
			break;

		default:
			elog(ERROR, "unrecognized object type %d", object->objtype);
			break;
	}
}

/*
 * Recursive helper for objtree_to_jsonb.
 */
static JsonbValue *
objtree_to_jsonb_rec(ObjTree *tree, JsonbParseState *state)
{
	slist_iter	iter;

	pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

	objtree_fmt_to_jsonb_element(state, tree);

	slist_foreach(iter, &tree->params)
	{
		ObjElem    *object = slist_container(ObjElem, node, iter.cur);
		JsonbValue	key;

		/* Push the key first */
		key.type = jbvString;
		key.val.string.len = strlen(object->name);
		key.val.string.val = object->name;
		pushJsonbValue(&state, WJB_KEY, &key);

		/* Then process the value according to its type */
		objtree_to_jsonb_element(state, object, WJB_VALUE);
	}

	return pushJsonbValue(&state, WJB_END_OBJECT, NULL);
}

/*
 * Subroutine for CREATE TABLE/CREATE DOMAIN deparsing.
 *
 * Given a table OID or domain OID, obtain its constraints and append them to
 * the given elements list.  The updated list is returned.
 *
 * This works for typed tables, regular tables, and domains.
 *
 * Note that CONSTRAINT_FOREIGN constraints are always ignored.
 */
static List *
obtainConstraints(List *elements, Oid relationId, Oid domainId)
{
	Relation	conRel;
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;
	ObjTree    *constr;

	/* Only one may be valid */
	Assert(OidIsValid(relationId) ^ OidIsValid(domainId));

	/*
	 * Scan pg_constraint to fetch all constraints linked to the given
	 * relation.
	 */
	conRel = table_open(ConstraintRelationId, AccessShareLock);
	if (OidIsValid(relationId))
	{
		ScanKeyInit(&key,
					Anum_pg_constraint_conrelid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(relationId));
		scan = systable_beginscan(conRel, ConstraintRelidTypidNameIndexId,
								  true, NULL, 1, &key);
	}
	else
	{
		ScanKeyInit(&key,
					Anum_pg_constraint_contypid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(domainId));
		scan = systable_beginscan(conRel, ConstraintTypidIndexId,
								  true, NULL, 1, &key);
	}

	/*
	 * For each constraint, add a node to the list of table elements.  In
	 * these nodes we include not only the printable information ("fmt"), but
	 * also separate attributes to indicate the type of constraint, for
	 * automatic processing.
	 */
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_constraint constrForm;
		char	   *contype;

		constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		switch (constrForm->contype)
		{
			case CONSTRAINT_CHECK:
				contype = "check";
				break;
			case CONSTRAINT_FOREIGN:
				continue;		/* not here */
			case CONSTRAINT_PRIMARY:
				contype = "primary key";
				break;
			case CONSTRAINT_UNIQUE:
				contype = "unique";
				break;
			case CONSTRAINT_TRIGGER:
				contype = "trigger";
				break;
			case CONSTRAINT_EXCLUSION:
				contype = "exclusion";
				break;
			default:
				elog(ERROR, "unrecognized constraint type");
		}

		/*
		 * "type" and "contype" are not part of the printable output, but are
		 * useful to programmatically distinguish these from columns and among
		 * different constraint types.
		 *
		 * XXX it might be useful to also list the column names in a PK, etc.
		 */
		constr = new_objtree_VA("CONSTRAINT %{name}I %{definition}s", 4,
								"type", ObjTypeString, "constraint",
								"contype", ObjTypeString, contype,
								"name", ObjTypeString, NameStr(constrForm->conname),
								"definition", ObjTypeString,
								pg_get_constraintdef_string(constrForm->oid));
		elements = lappend(elements, new_object_object(constr));
	}

	systable_endscan(scan);
	table_close(conRel, AccessShareLock);

	return elements;
}

/*
 * Return an index definition, split into several pieces.
 *
 * A large amount of code is duplicated from  pg_get_indexdef_worker, but
 * control flow is different enough that it doesn't seem worth keeping them
 * together.
 */
static void
pg_get_indexdef_detailed(Oid indexrelid,
						 char **index_am,
						 char **definition,
						 char **reloptions,
						 char **tablespace,
						 char **whereClause)
{
	HeapTuple	ht_idx;
	HeapTuple	ht_idxrel;
	HeapTuple	ht_am;
	Form_pg_index idxrec;
	Form_pg_class idxrelrec;
	Form_pg_am	amrec;
	IndexAmRoutine *amroutine;
	List	   *indexprs;
	ListCell   *indexpr_item;
	List	   *context;
	Oid			indrelid;
	int			keyno;
	Datum		indcollDatum;
	Datum		indclassDatum;
	Datum		indoptionDatum;
	bool		isnull;
	oidvector  *indcollation;
	oidvector  *indclass;
	int2vector *indoption;
	StringInfoData definitionBuf;

	*tablespace = NULL;
	*whereClause = NULL;

	/* Fetch the pg_index tuple by the Oid of the index */
	ht_idx = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexrelid));
	if (!HeapTupleIsValid(ht_idx))
		elog(ERROR, "cache lookup failed for index with OID %u", indexrelid);
	idxrec = (Form_pg_index) GETSTRUCT(ht_idx);

	indrelid = idxrec->indrelid;
	Assert(indexrelid == idxrec->indexrelid);

	/* Must get indcollation, indclass, and indoption the hard way */
	indcollDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
								   Anum_pg_index_indcollation, &isnull);
	Assert(!isnull);
	indcollation = (oidvector *) DatumGetPointer(indcollDatum);

	indclassDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
									Anum_pg_index_indclass, &isnull);
	Assert(!isnull);
	indclass = (oidvector *) DatumGetPointer(indclassDatum);

	indoptionDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
									 Anum_pg_index_indoption, &isnull);
	Assert(!isnull);
	indoption = (int2vector *) DatumGetPointer(indoptionDatum);

	/* Fetch the pg_class tuple of the index relation */
	ht_idxrel = SearchSysCache1(RELOID, ObjectIdGetDatum(indexrelid));
	if (!HeapTupleIsValid(ht_idxrel))
		elog(ERROR, "cache lookup failed for relation with OID %u", indexrelid);
	idxrelrec = (Form_pg_class) GETSTRUCT(ht_idxrel);

	/* Fetch the pg_am tuple of the index' access method */
	ht_am = SearchSysCache1(AMOID, ObjectIdGetDatum(idxrelrec->relam));
	if (!HeapTupleIsValid(ht_am))
		elog(ERROR, "cache lookup failed for access method with OID %u",
			 idxrelrec->relam);
	amrec = (Form_pg_am) GETSTRUCT(ht_am);

	/*
	 * Get the index expressions, if any.  (NOTE: we do not use the relcache
	 * versions of the expressions and predicate, because we want to display
	 * non-const-folded expressions.)
	 */
	if (!heap_attisnull(ht_idx, Anum_pg_index_indexprs, NULL))
	{
		Datum		exprsDatum;
		char	   *exprsString;

		exprsDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
									 Anum_pg_index_indexprs, &isnull);
		Assert(!isnull);
		exprsString = TextDatumGetCString(exprsDatum);
		indexprs = (List *) stringToNode(exprsString);
		pfree(exprsString);
	}
	else
		indexprs = NIL;

	indexpr_item = list_head(indexprs);

	context = deparse_context_for(get_rel_name(indrelid), indrelid);

	initStringInfo(&definitionBuf);

	/* Output index AM */
	*index_am = pstrdup(quote_identifier(NameStr(amrec->amname)));

	/* Fetch the index AM's API struct */
	amroutine = GetIndexAmRoutine(amrec->amhandler);

	/*
	 * Output index definition.  Note the outer parens must be supplied by
	 * caller.
	 */
	appendStringInfoString(&definitionBuf, "(");
	for (keyno = 0; keyno < idxrec->indnatts; keyno++)
	{
		AttrNumber	attnum = idxrec->indkey.values[keyno];
		int16		opt = indoption->values[keyno];
		Oid			keycoltype;
		Oid			keycolcollation;

		/* Print INCLUDE to divide key and non-key attrs. */
		if (keyno == idxrec->indnkeyatts)
		{
			appendStringInfoString(&definitionBuf, ") INCLUDE (");
		}
		else
			appendStringInfoString(&definitionBuf, keyno == 0 ? "" : ", ");

		if (attnum != 0)
		{
			/* Simple index column */
			char	   *attname;
			int32		keycoltypmod;

			attname = get_attname(indrelid, attnum, false);
			appendStringInfoString(&definitionBuf, quote_identifier(attname));
			get_atttypetypmodcoll(indrelid, attnum,
								  &keycoltype, &keycoltypmod,
								  &keycolcollation);
		}
		else
		{
			/* Expressional index */
			Node	   *indexkey;
			char	   *str;

			if (indexpr_item == NULL)
				elog(ERROR, "too few entries in indexprs list");
			indexkey = (Node *) lfirst(indexpr_item);
			indexpr_item = lnext(indexprs, indexpr_item);

			/* Deparse */
			str = deparse_expression(indexkey, context, false, false);

			/* Need parens if it's not a bare function call */
			if (indexkey && IsA(indexkey, FuncExpr) &&
				((FuncExpr *) indexkey)->funcformat == COERCE_EXPLICIT_CALL)
				appendStringInfoString(&definitionBuf, str);
			else
				appendStringInfo(&definitionBuf, "(%s)", str);

			keycoltype = exprType(indexkey);
			keycolcollation = exprCollation(indexkey);
		}

		/* Print additional decoration for (selected) key columns, even if default */
		if (keyno < idxrec->indnkeyatts)
		{
			Oid indcoll = indcollation->values[keyno];
			if (OidIsValid(indcoll))
				appendStringInfo(&definitionBuf, " COLLATE %s",
								generate_collation_name((indcoll)));

			/* Add the operator class name, even if default */
			get_opclass_name(indclass->values[keyno], InvalidOid, &definitionBuf);

			/* Add options if relevant */
			if (amroutine->amcanorder)
			{
				/* If it supports sort ordering, report DESC and NULLS opts */
				if (opt & INDOPTION_DESC)
				{
					appendStringInfoString(&definitionBuf, " DESC");
					/* NULLS FIRST is the default in this case */
					if (!(opt & INDOPTION_NULLS_FIRST))
						appendStringInfoString(&definitionBuf, " NULLS LAST");
				}
				else
				{
					if (opt & INDOPTION_NULLS_FIRST)
						appendStringInfoString(&definitionBuf, " NULLS FIRST");
				}
			}

			/* XXX excludeOps thingy was here; do we need anything? */
		}
	}
	appendStringInfoString(&definitionBuf, ")");
	*definition = definitionBuf.data;

	/* Output reloptions */
	*reloptions = flatten_reloptions(indexrelid);

	/* Output tablespace */
	{
		Oid			tblspc;

		tblspc = get_rel_tablespace(indexrelid);
		if (OidIsValid(tblspc))
			*tablespace = pstrdup(quote_identifier(get_tablespace_name(tblspc)));
	}

	/* Report index predicate, if any */
	if (!heap_attisnull(ht_idx, Anum_pg_index_indpred, NULL))
	{
		Node	   *node;
		Datum		predDatum;
		char	   *predString;

		/* Convert text string to node tree */
		predDatum = SysCacheGetAttr(INDEXRELID, ht_idx,
									Anum_pg_index_indpred, &isnull);
		Assert(!isnull);
		predString = TextDatumGetCString(predDatum);
		node = (Node *) stringToNode(predString);
		pfree(predString);

		/* Deparse */
		*whereClause = deparse_expression(node, context, false, false);
	}

	/* Clean up */
	ReleaseSysCache(ht_idx);
	ReleaseSysCache(ht_idxrel);
	ReleaseSysCache(ht_am);
}

/*
 * Obtain the deparsed default value for the given column of the given table.
 *
 * Caller must have set a correct deparse context.
 */
static char *
RelationGetColumnDefault(Relation rel, AttrNumber attno, List *dpcontext,
						 List **exprs)
{
	Node	   *defval;
	char	   *defstr;

	defval = build_column_default(rel, attno);
	defstr = deparse_expression(defval, dpcontext, false, false);

	/* Collect the expression for later replication safety checks */
	if (exprs)
		*exprs = lappend(*exprs, defval);

	return defstr;
}

/*
 * Obtain the deparsed partition bound expression for the given table.
 */
static char *
RelationGetPartitionBound(Oid relid)
{
	Datum		deparsed;
	Datum		boundDatum;
	bool		isnull;
	HeapTuple	tuple;

	tuple = SearchSysCache1(RELOID, relid);
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation with OID %u", relid);

	boundDatum = SysCacheGetAttr(RELOID, tuple,
								 Anum_pg_class_relpartbound,
								 &isnull);

	deparsed = DirectFunctionCall2(pg_get_expr,
								   CStringGetTextDatum(TextDatumGetCString(boundDatum)),
								   relid);

	ReleaseSysCache(tuple);

	return TextDatumGetCString(deparsed);
}

/*
 * Deparse a ColumnDef node within a regular (non-typed) table creation.
 *
 * NOT NULL constraints in the column definition are emitted directly in the
 * column definition by this routine; other constraints must be emitted
 * elsewhere (the info in the parse node is incomplete anyway).
 *
 * Verbose syntax
 * %{name}I %{coltype}T %{compression}s %{default}s %{not_null}s %{collation}s
 */
static ObjTree *
deparse_ColumnDef(Relation relation, List *dpcontext, bool composite,
				  ColumnDef *coldef, bool is_alter, List **exprs)
{
	ObjTree    *ret;
	ObjTree    *tmp_obj;
	Oid			relid = RelationGetRelid(relation);
	HeapTuple	attrTup;
	Form_pg_attribute attrForm;
	Oid			typid;
	int32		typmod;
	Oid			typcollation;
	bool		saw_notnull;
	ListCell   *cell;

	/*
	 * Inherited columns without local definitions must not be emitted.
	 *
	 * XXX maybe it is useful to have them with "present = false" or some
	 * such?
	 */
	if (!coldef->is_local)
		return NULL;

	attrTup = SearchSysCacheAttName(relid, coldef->colname);
	if (!HeapTupleIsValid(attrTup))
		elog(ERROR, "could not find cache entry for column \"%s\" of relation %u",
			 coldef->colname, relid);
	attrForm = (Form_pg_attribute) GETSTRUCT(attrTup);

	get_atttypetypmodcoll(relid, attrForm->attnum,
						  &typid, &typmod, &typcollation);

	ret = new_objtree_VA("%{name}I %{coltype}T", 3,
						 "type", ObjTypeString, "column",
						 "name", ObjTypeString, coldef->colname,
						 "coltype", ObjTypeObject,
						 new_objtree_for_type(typid, typmod));

	if (!composite)
		append_string_object(ret, "STORAGE %{colstorage}s", "colstorage",
							 get_type_storage(attrForm->attstorage));

	/* USING clause */
	tmp_obj = new_objtree("COMPRESSION");
	if (coldef->compression)
		append_string_object(tmp_obj, "%{compression_method}I",
							 "compression_method", coldef->compression);
	else
	{
		append_null_object(tmp_obj, "%{compression_method}I");
		append_not_present(tmp_obj);
	}
	append_object_object(ret, "%{compression}s", tmp_obj);

	tmp_obj = new_objtree("COLLATE");
	if (OidIsValid(typcollation))
		append_object_object(tmp_obj, "%{name}D",
							 new_objtree_for_qualname_id(CollationRelationId,
														 typcollation));
	else
		append_not_present(tmp_obj);
	append_object_object(ret, "%{collation}s", tmp_obj);

	if (!composite)
	{
		Oid			seqrelid = InvalidOid;

		/*
		 * Emit a NOT NULL declaration if necessary.  Note that we cannot
		 * trust pg_attribute.attnotnull here, because that bit is also set
		 * when primary keys are specified; we must not emit a NOT NULL
		 * constraint in that case, unless explicitly specified.  Therefore,
		 * we scan the list of constraints attached to this column to
		 * determine whether we need to emit anything. (Fortunately, NOT NULL
		 * constraints cannot be table constraints.)
		 *
		 * In the ALTER TABLE cases, we also add a NOT NULL if the colDef is
		 * marked is_not_null.
		 */
		saw_notnull = false;
		foreach(cell, coldef->constraints)
		{
			Constraint *constr = (Constraint *) lfirst(cell);

			if (constr->contype == CONSTR_NOTNULL)
			{
				saw_notnull = true;
				break;
			}
		}

		if (is_alter && coldef->is_not_null)
			saw_notnull = true;

		append_string_object(ret, "%{not_null}s", "not_null",
							 saw_notnull ? "NOT NULL" : "");

		tmp_obj = new_objtree("DEFAULT");
		if (attrForm->atthasdef &&
			coldef->generated != ATTRIBUTE_GENERATED_STORED)
		{
			char	   *defstr;

			defstr = RelationGetColumnDefault(relation, attrForm->attnum,
											  dpcontext, exprs);

			append_string_object(tmp_obj, "%{default}s", "default", defstr);
		}
		else
			append_not_present(tmp_obj);
		append_object_object(ret, "%{default}s", tmp_obj);

		/* IDENTITY COLUMN */
		if (coldef->identity)
		{
			Oid			attno = get_attnum(relid, coldef->colname);

			seqrelid = getIdentitySequence(relid, attno, true);
			if (OidIsValid(seqrelid) && coldef->identitySequence)
				seqrelid = RangeVarGetRelid(coldef->identitySequence, NoLock, false);
		}

		if (OidIsValid(seqrelid))
		{
			tmp_obj = deparse_ColumnIdentity(seqrelid, coldef->identity, is_alter);
			append_object_object(ret, "%{identity_column}s", tmp_obj);
		}

		/* GENERATED COLUMN EXPRESSION */
		tmp_obj = new_objtree("GENERATED ALWAYS AS");
		if (coldef->generated == ATTRIBUTE_GENERATED_STORED)
		{
			char	   *defstr;

			defstr = RelationGetColumnDefault(relation, attrForm->attnum,
											  dpcontext, exprs);
			append_string_object(tmp_obj, "(%{generation_expr}s) STORED",
								 "generation_expr", defstr);
		}
		else
			append_not_present(tmp_obj);

		append_object_object(ret, "%{generated_column}s", tmp_obj);
	}

	ReleaseSysCache(attrTup);

	return ret;
}

/*
 * Deparse a ColumnDef node within a typed table creation.	This is simpler
 * than the regular case, because we don't have to emit the type declaration,
 * collation, or default.  Here we only return something if the column is being
 * declared NOT NULL.
 *
 * As in deparse_ColumnDef, any other constraint is processed elsewhere.
 *
 * Verbose syntax
 * %{name}I WITH OPTIONS %{not_null}s %{default}s.
 */
static ObjTree *
deparse_ColumnDef_typed(Relation relation, List *dpcontext, ColumnDef *coldef)
{
	ObjTree    *ret = NULL;
	ObjTree    *tmp_obj;
	Oid			relid = RelationGetRelid(relation);
	HeapTuple	attrTup;
	Form_pg_attribute attrForm;
	Oid			typid;
	int32		typmod;
	Oid			typcollation;
	bool		saw_notnull;
	ListCell   *cell;

	attrTup = SearchSysCacheAttName(relid, coldef->colname);
	if (!HeapTupleIsValid(attrTup))
		elog(ERROR, "could not find cache entry for column \"%s\" of relation %u",
			 coldef->colname, relid);
	attrForm = (Form_pg_attribute) GETSTRUCT(attrTup);

	get_atttypetypmodcoll(relid, attrForm->attnum,
						  &typid, &typmod, &typcollation);

	/*
	 * Search for a NOT NULL declaration. As in deparse_ColumnDef, we rely on
	 * finding a constraint on the column rather than coldef->is_not_null.
	 * (This routine is never used for ALTER cases.)
	 */
	saw_notnull = false;
	foreach(cell, coldef->constraints)
	{
		Constraint *constr = (Constraint *) lfirst(cell);

		if (constr->contype == CONSTR_NOTNULL)
		{
			saw_notnull = true;
			break;
		}
	}

	if (!saw_notnull && !attrForm->atthasdef)
	{
		ReleaseSysCache(attrTup);
		return NULL;
	}

	tmp_obj = new_objtree("DEFAULT");
	if (attrForm->atthasdef)
	{
		char	   *defstr;

		defstr = RelationGetColumnDefault(relation, attrForm->attnum,
										  dpcontext, NULL);

		append_string_object(tmp_obj, "%{default}s", "default", defstr);
	}
	else
		append_not_present(tmp_obj);

	ret = new_objtree_VA("%{name}I WITH OPTIONS %{not_null}s %{default}s", 4,
						 "type", ObjTypeString, "column",
						 "name", ObjTypeString, coldef->colname,
						 "not_null", ObjTypeString,
						 saw_notnull ? "NOT NULL" : "",
						 "default", ObjTypeObject, tmp_obj);

	/* Generated columns are not supported on typed tables, so we are done */

	ReleaseSysCache(attrTup);

	return ret;
}

/*
 * Deparse the definition of column identity.
 *
 * Verbose syntax
 * SET GENERATED %{option}s %{identity_type}s %{seq_definition: }s
 * 	OR
 * GENERATED %{option}s AS IDENTITY %{identity_type}s ( %{seq_definition: }s )
 */
static ObjTree *
deparse_ColumnIdentity(Oid seqrelid, char identity, bool alter_table)
{
	ObjTree    *ret;
	ObjTree    *ident_obj;
	List	   *elems = NIL;
	Form_pg_sequence seqform;
	Sequence_values *seqvalues;
	char	   *identfmt;
	char	   *objfmt;

	if (alter_table)
	{
		identfmt = "SET GENERATED ";
		objfmt = "%{option}s";
	}
	else
	{
		identfmt = "GENERATED ";
		objfmt = "%{option}s AS IDENTITY";
	}

	ident_obj = new_objtree(identfmt);

	if (identity == ATTRIBUTE_IDENTITY_ALWAYS)
		append_string_object(ident_obj, objfmt, "option", "ALWAYS");
	else if (identity == ATTRIBUTE_IDENTITY_BY_DEFAULT)
		append_string_object(ident_obj, objfmt, "option", "BY DEFAULT");
	else
		append_not_present(ident_obj);

	ret = new_objtree_VA("%{identity_type}s", 1,
						 "identity_type", ObjTypeObject, ident_obj);

	seqvalues = get_sequence_values(seqrelid);
	seqform = seqvalues->seqform;

	/* Definition elements */
	elems = lappend(elems, deparse_Seq_Cache(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_Cycle(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_IncrementBy(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_Minvalue(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_Maxvalue(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_Startwith(seqform, alter_table));
	elems = lappend(elems, deparse_Seq_Restart(seqvalues->last_value));
	/* We purposefully do not emit OWNED BY here */

	if (alter_table)
		append_array_object(ret, "%{seq_definition: }s", elems);
	else
		append_array_object(ret, "( %{seq_definition: }s )", elems);

	return ret;
}

/*
 * ... ALTER COLUMN ... SET/RESET (...)
 *
 * Verbose syntax
 * ALTER COLUMN %{column}I RESET|SET (%{options:, }s)
 */
static ObjTree *
deparse_ColumnSetOptions(AlterTableCmd *subcmd)
{
	List	   *sets = NIL;
	ListCell   *cell;
	ObjTree    *ret;
	bool		is_reset = subcmd->subtype == AT_ResetOptions;

	ret = new_objtree_VA("ALTER COLUMN %{column}I %{option}s", 2,
						 "column", ObjTypeString, subcmd->name,
						 "option", ObjTypeString, is_reset ? "RESET" : "SET");

	foreach(cell, (List *) subcmd->def)
	{
		DefElem    *elem;
		ObjTree    *set;

		elem = (DefElem *) lfirst(cell);
		set = deparse_DefElem(elem, is_reset);
		sets = lappend(sets, new_object_object(set));
	}

	Assert(sets);
	append_array_object(ret, "(%{options:, }s)", sets);

	return ret;
}

/*
 * ... ALTER COLUMN ... SET/RESET (...)
 *
 * Verbose syntax
 * RESET|SET (%{options:, }s)
 */
static ObjTree *
deparse_RelSetOptions(AlterTableCmd *subcmd)
{
	List	   *sets = NIL;
	ListCell   *cell;
	bool		is_reset = subcmd->subtype == AT_ResetRelOptions;

	foreach(cell, (List *) subcmd->def)
	{
		DefElem    *elem;
		ObjTree    *set;

		elem = (DefElem *) lfirst(cell);
		set = deparse_DefElem(elem, is_reset);
		sets = lappend(sets, new_object_object(set));
	}

	Assert(sets);

	return new_objtree_VA("%{set_reset}s (%{options:, }s)", 2,
						  "set_reset", ObjTypeString, is_reset ? "RESET" : "SET",
						  "options", ObjTypeArray, sets);
}

/*
 * Deparse DefElems, as used e.g. by ALTER COLUMN ... SET, into a list of SET
 * (...)  or RESET (...) contents.
 *
 * Verbose syntax
 * %{label}s = %{value}L
 */
static ObjTree *
deparse_DefElem(DefElem *elem, bool is_reset)
{
	ObjTree    *ret;
	ObjTree    *optname = new_objtree("");

	if (elem->defnamespace != NULL)
		append_string_object(optname, "%{schema}I.", "schema",
							 elem->defnamespace);

	append_string_object(optname, "%{label}I", "label", elem->defname);

	ret = new_objtree_VA("%{label}s", 1,
						 "label", ObjTypeObject, optname);

	if (!is_reset)
		append_string_object(ret, "= %{value}L", "value",
							 elem->arg ? defGetString(elem) :
							 defGetBoolean(elem) ? "TRUE" : "FALSE");

	return ret;
}

/*
 * Deparse the INHERITS relations.
 *
 * Given a table OID, return a schema-qualified table list representing
 * the parent tables.
 */
static List *
deparse_InhRelations(Oid objectId)
{
	List	   *parents = NIL;
	Relation	inhRel;
	SysScanDesc scan;
	ScanKeyData key;
	HeapTuple	tuple;

	inhRel = table_open(InheritsRelationId, RowExclusiveLock);

	ScanKeyInit(&key,
				Anum_pg_inherits_inhrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(objectId));

	scan = systable_beginscan(inhRel, InheritsRelidSeqnoIndexId,
							  true, NULL, 1, &key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		ObjTree    *parent;
		Form_pg_inherits formInh = (Form_pg_inherits) GETSTRUCT(tuple);

		parent = new_objtree_for_qualname_id(RelationRelationId,
											 formInh->inhparent);
		parents = lappend(parents, new_object_object(parent));
	}

	systable_endscan(scan);
	table_close(inhRel, RowExclusiveLock);

	return parents;
}

/*
 * Deparse the ON COMMIT ... clause for CREATE ... TEMPORARY ...
 *
 * Verbose syntax
 * ON COMMIT %{on_commit_value}s
 */
static ObjTree *
deparse_OnCommitClause(OnCommitAction option)
{
	ObjTree    *ret  = new_objtree("ON COMMIT");
	switch (option)
	{
		case ONCOMMIT_DROP:
			append_string_object(ret, "%{on_commit_value}s",
								 "on_commit_value", "DROP");
			break;

		case ONCOMMIT_DELETE_ROWS:
			append_string_object(ret, "%{on_commit_value}s",
								 "on_commit_value", "DELETE ROWS");
			break;

		case ONCOMMIT_PRESERVE_ROWS:
			append_string_object(ret, "%{on_commit_value}s",
								 "on_commit_value", "PRESERVE ROWS");
			break;

		case ONCOMMIT_NOOP:
			append_null_object(ret, "%{on_commit_value}s");
			append_not_present(ret);
			break;
	}

	return ret;
}

/*
 * Deparse the sequence CACHE option.
 *
 * Verbose syntax
 * SET CACHE %{value}s
 * OR
 * CACHE %{value}
 */
static inline ObjElem *
deparse_Seq_Cache(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *tmpstr;
	char	   *fmt;

	fmt = alter_table ? "SET CACHE %{value}s" : "CACHE %{value}s";

	tmpstr = psprintf(INT64_FORMAT, seqdata->seqcache);
	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "cache",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the sequence CYCLE option.
 *
 * Verbose syntax
 * SET %{no}s CYCLE
 * OR
 * %{no}s CYCLE
 */
static inline ObjElem *
deparse_Seq_Cycle(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *fmt;

	fmt = alter_table ? "SET %{no}s CYCLE" : "%{no}s CYCLE";

	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "cycle",
						 "no", ObjTypeString,
						 seqdata->seqcycle ? "" : "NO");

	return new_object_object(ret);
}

/*
 * Deparse the sequence INCREMENT BY option.
 *
 * Verbose syntax
 * SET INCREMENT BY %{value}s
 * OR
 * INCREMENT BY %{value}s
 */
static inline ObjElem *
deparse_Seq_IncrementBy(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *tmpstr;
	char	   *fmt;

	fmt = alter_table ? "SET INCREMENT BY %{value}s" : "INCREMENT BY %{value}s";

	tmpstr = psprintf(INT64_FORMAT, seqdata->seqincrement);
	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "seqincrement",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the sequence MAXVALUE option.
 *
 * Verbose syntax
 * SET MAXVALUE %{value}s
 * OR
 * MAXVALUE %{value}s
 */
static inline ObjElem *
deparse_Seq_Maxvalue(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *tmpstr;
	char	   *fmt;

	fmt = alter_table ? "SET MAXVALUE %{value}s" : "MAXVALUE %{value}s";

	tmpstr = psprintf(INT64_FORMAT, seqdata->seqmax);
	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "maxvalue",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the sequence MINVALUE option.
 *
 * Verbose syntax
 * SET MINVALUE %{value}s
 * OR
 * MINVALUE %{value}s
 */
static inline ObjElem *
deparse_Seq_Minvalue(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *tmpstr;
	char	   *fmt;

	fmt = alter_table ? "SET MINVALUE %{value}s" : "MINVALUE %{value}s";

	tmpstr = psprintf(INT64_FORMAT, seqdata->seqmin);
	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "minvalue",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the sequence RESTART option.
 *
 * Verbose syntax
 * RESTART %{value}s
 */
static inline ObjElem *
deparse_Seq_Restart(int64 last_value)
{
	ObjTree    *ret;
	char	   *tmpstr;

	tmpstr = psprintf(INT64_FORMAT, last_value);
	ret = new_objtree_VA("RESTART %{value}s", 2,
						 "clause", ObjTypeString, "restart",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the sequence AS option.
 *
 * Verbose syntax
 * AS %{seqtype}T
 */
static inline ObjElem *
deparse_Seq_As(Form_pg_sequence seqdata)
{
	ObjTree    *ret;

	ret = new_objtree("AS");
	if (OidIsValid(seqdata->seqtypid))
		append_object_object(ret, "%{seqtype}T",
							 new_objtree_for_type(seqdata->seqtypid, -1));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the sequence START WITH option.
 *
 * Verbose syntax
 * SET START WITH %{value}s
 * OR
 * START WITH %{value}s
 */
static inline ObjElem *
deparse_Seq_Startwith(Form_pg_sequence seqdata, bool alter_table)
{
	ObjTree    *ret;
	char	   *tmpstr;
	char	   *fmt;

	fmt = alter_table ? "SET START WITH %{value}s" : "START WITH %{value}s";

	tmpstr = psprintf(INT64_FORMAT, seqdata->seqstart);
	ret = new_objtree_VA(fmt, 2,
						 "clause", ObjTypeString, "start",
						 "value", ObjTypeString, tmpstr);

	return new_object_object(ret);
}

/*
 * Deparse the type STORAGE option.
 *
 * Verbose syntax
 * STORAGE=%{value}s
 */
static inline ObjElem *
deparse_Type_Storage(Form_pg_type typForm)
{
	ObjTree    *ret;
	ret = new_objtree_VA("STORAGE = %{value}s", 2,
						 "clause", ObjTypeString, "storage",
						 "value", ObjTypeString, get_type_storage(typForm->typstorage));

	return new_object_object(ret);
}

/*
 * Deparse the type RECEIVE option.
 *
 * Verbose syntax
 * RECEIVE=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Receive(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("RECEIVE=", 1,
						 "clause", ObjTypeString, "receive");
	if (OidIsValid(typForm->typreceive))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typreceive));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the type SEND option.
 *
 * Verbose syntax
 * SEND=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Send(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("SEND=", 1,
						 "clause", ObjTypeString, "send");
	if (OidIsValid(typForm->typsend))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typsend));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the type typmod_in option.
 *
 * Verbose syntax
 * TYPMOD_IN=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Typmod_In(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("TYPMOD_IN=", 1,
						 "clause", ObjTypeString, "typmod_in");
	if (OidIsValid(typForm->typmodin))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typmodin));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the type typmod_out option.
 *
 * Verbose syntax
 * TYPMOD_OUT=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Typmod_Out(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("TYPMOD_OUT=", 1,
						 "clause", ObjTypeString, "typmod_out");
	if (OidIsValid(typForm->typmodout))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typmodout));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the type analyze option.
 *
 * Verbose syntax
 * ANALYZE=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Analyze(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("ANALYZE=", 1,
						 "clause", ObjTypeString, "analyze");
	if (OidIsValid(typForm->typanalyze))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typanalyze));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Deparse the type subscript option.
 *
 * Verbose syntax
 * SUBSCRIPT=%{procedure}D
 */
static inline ObjElem *
deparse_Type_Subscript(Form_pg_type typForm)
{
	ObjTree    *ret;

	ret = new_objtree_VA("SUBSCRIPT=", 1,
						 "clause", ObjTypeString, "subscript");
	if (OidIsValid(typForm->typsubscript))
		append_object_object(ret, "%{procedure}D",
							 new_objtree_for_qualname_id(ProcedureRelationId,
														 typForm->typsubscript));
	else
		append_not_present(ret);

	return new_object_object(ret);
}

/*
 * Subroutine for CREATE TABLE deparsing.
 *
 * Deal with all the table elements (columns and constraints).
 *
 * Note we ignore constraints in the parse node here; they are extracted from
 * system catalogs instead.
 */
static List *
deparse_TableElements(Relation relation, List *tableElements, List *dpcontext,
					  bool typed, bool composite)
{
	List	   *elements = NIL;
	ListCell   *lc;

	foreach(lc, tableElements)
	{
		Node	   *elt = (Node *) lfirst(lc);

		switch (nodeTag(elt))
		{
			case T_ColumnDef:
				{
					ObjTree    *tree;

					tree = typed ?
						deparse_ColumnDef_typed(relation, dpcontext,
												(ColumnDef *) elt) :
						deparse_ColumnDef(relation, dpcontext,
										  composite, (ColumnDef *) elt,
										  false, NULL);
					if (tree != NULL)
						elements = lappend(elements, new_object_object(tree));
				}
				break;
			case T_Constraint:
				break;
			default:
				elog(ERROR, "invalid node type %d", nodeTag(elt));
		}
	}

	return elements;
}

/*
 * Deparse a CreateSeqStmt.
 *
 * Given a sequence OID and the parse tree that created it, return an ObjTree
 * representing the creation command.
 *
 * Verbose syntax
 * CREATE %{persistence}s SEQUENCE %{identity}D
 */
static ObjTree *
deparse_CreateSeqStmt(Oid objectId, Node *parsetree)
{
	ObjTree    *ret;
	Relation	relation;
	List	   *elems = NIL;
	Form_pg_sequence seqform;
	Sequence_values *seqvalues;
	CreateSeqStmt *createSeqStmt = (CreateSeqStmt *) parsetree;

	/*
	 * Sequence for IDENTITY COLUMN output separately (via CREATE TABLE or
	 * ALTER TABLE); return empty here.
	 */
	if (createSeqStmt->for_identity)
		return NULL;

	seqvalues = get_sequence_values(objectId);
	seqform = seqvalues->seqform;

	/* Definition elements */
	elems = lappend(elems, deparse_Seq_Cache(seqform, false));
	elems = lappend(elems, deparse_Seq_Cycle(seqform, false));
	elems = lappend(elems, deparse_Seq_IncrementBy(seqform, false));
	elems = lappend(elems, deparse_Seq_Minvalue(seqform, false));
	elems = lappend(elems, deparse_Seq_Maxvalue(seqform, false));
	elems = lappend(elems, deparse_Seq_Startwith(seqform, false));
	elems = lappend(elems, deparse_Seq_Restart(seqvalues->last_value));
	elems = lappend(elems, deparse_Seq_As(seqform));

	/* We purposefully do not emit OWNED BY here */

	relation = relation_open(objectId, AccessShareLock);

	ret = new_objtree_VA("CREATE %{persistence}s SEQUENCE %{if_not_exists}s %{identity}D %{definition: }s", 4,
						 "persistence", ObjTypeString,
						 get_persistence_str(relation->rd_rel->relpersistence),
						 "if_not_exists", ObjTypeString,
						 createSeqStmt->if_not_exists ? "IF NOT EXISTS" : "",
						 "identity", ObjTypeObject,
						 new_objtree_for_qualname(relation->rd_rel->relnamespace,
												  RelationGetRelationName(relation)),
						 "definition", ObjTypeArray, elems);

	relation_close(relation, AccessShareLock);

	return ret;
}

/*
 * Deparse an IndexStmt.
 *
 * Given an index OID and the parse tree that created it, return an ObjTree
 * representing the creation command.
 *
 * If the index corresponds to a constraint, NULL is returned.
 *
 * Verbose syntax
 * CREATE %{unique}s INDEX %{concurrently}s %{if_not_exists}s %{name}I ON
 * %{table}D USING %{index_am}s %{definition}s %{with}s %{tablespace}s
 * %{where_clause}s %{nulls_not_distinct}s
 */
static ObjTree *
deparse_IndexStmt(Oid objectId, Node *parsetree)
{
	IndexStmt  *node = (IndexStmt *) parsetree;
	ObjTree    *ret;
	ObjTree    *tmp_obj;
	Relation	idxrel;
	Relation	heaprel;
	char	   *index_am;
	char	   *definition;
	char	   *reloptions;
	char	   *tablespace;
	char	   *whereClause;

	if (node->primary || node->isconstraint)
	{
		/*
		 * Indexes for PRIMARY KEY and other constraints are output
		 * separately; return empty here.
		 */
		return NULL;
	}

	idxrel = relation_open(objectId, AccessShareLock);
	heaprel = relation_open(idxrel->rd_index->indrelid, AccessShareLock);

	pg_get_indexdef_detailed(objectId,
							 &index_am, &definition, &reloptions,
							 &tablespace, &whereClause);

	ret = new_objtree_VA("CREATE %{unique}s INDEX %{concurrently}s %{if_not_exists}s %{name}I ON %{only}s %{table}D USING %{index_am}s %{definition}s", 8,
						 "unique", ObjTypeString,
						 node->unique ? "UNIQUE" : "",
						 "concurrently", ObjTypeString,
						 node->concurrent ? "CONCURRENTLY" : "",
						 "if_not_exists", ObjTypeString,
						 node->if_not_exists ? "IF NOT EXISTS" : "",
						 "only", ObjTypeString,
						 node->relation->inh ? "" : "ONLY",
						 "name", ObjTypeString,
						 RelationGetRelationName(idxrel),
						 "table", ObjTypeObject,
						 new_objtree_for_qualname(heaprel->rd_rel->relnamespace,
												  RelationGetRelationName(heaprel)),
						 "index_am", ObjTypeString, index_am,
						 "definition", ObjTypeString, definition);

	/* reloptions */
	tmp_obj = new_objtree("WITH");
	if (reloptions)
		append_string_object(tmp_obj, "(%{opts}s)", "opts", reloptions);
	else
		append_not_present(tmp_obj);
	append_object_object(ret, "%{with}s", tmp_obj);

	/* tablespace */
	tmp_obj = new_objtree("TABLESPACE");
	if (tablespace)
		append_string_object(tmp_obj, "%{tablespace}s", "tablespace", tablespace);
	else
		append_not_present(tmp_obj);
	append_object_object(ret, "%{tablespace}s", tmp_obj);

	/* WHERE clause */
	tmp_obj = new_objtree("WHERE");
	if (whereClause)
		append_string_object(tmp_obj, "%{where}s", "where", whereClause);
	else
		append_not_present(tmp_obj);
	append_object_object(ret, "%{where_clause}s", tmp_obj);

	/* nulls_not_distinct */
	if (node->nulls_not_distinct)
		append_format_string(ret, "NULLS NOT DISTINCT");
	else
		append_format_string(ret, "NULLS DISTINCT");

	table_close(idxrel, AccessShareLock);
	table_close(heaprel, AccessShareLock);

	return ret;
}

/*
 * Deparse a CreateStmt (CREATE TABLE).
 *
 * Given a table OID and the parse tree that created it, return an ObjTree
 * representing the creation command.
 *
 * Verbose syntax
 * CREATE %{persistence}s TABLE %{if_not_exists}s %{identity}D [OF
 * %{of_type}T | PARTITION OF %{parent_identity}D] %{table_elements}s
 * %{inherits}s %{partition_by}s %{access_method}s %{with_clause}s
 * %{on_commit}s %{tablespace}s
 */
static ObjTree *
deparse_CreateStmt(Oid objectId, Node *parsetree)
{
	CreateStmt *node = (CreateStmt *) parsetree;
	Relation	relation = relation_open(objectId, AccessShareLock);
	List	   *dpcontext;
	ObjTree    *ret;
	ObjTree    *tmp_obj;
	List	   *list = NIL;
	ListCell   *cell;

	ret = new_objtree_VA("CREATE %{persistence}s TABLE %{if_not_exists}s %{identity}D", 3,
						 "persistence", ObjTypeString,
						 get_persistence_str(relation->rd_rel->relpersistence),
						 "if_not_exists", ObjTypeString,
						 node->if_not_exists ? "IF NOT EXISTS" : "",
						 "identity", ObjTypeObject,
						 new_objtree_for_qualname(relation->rd_rel->relnamespace,
												  RelationGetRelationName(relation)));

	dpcontext = deparse_context_for(RelationGetRelationName(relation),
									objectId);

	/*
	 * Typed tables and partitions use a slightly different format string: we
	 * must not put table_elements with parents directly in the fmt string,
	 * because if there are no options the parentheses must not be emitted;
	 * and also, typed tables do not allow for inheritance.
	 */
	if (node->ofTypename || node->partbound)
	{
		List	   *tableelts = NIL;

		/*
		 * We can't put table elements directly in the fmt string as an array
		 * surrounded by parentheses here, because an empty clause would cause
		 * a syntax error.  Therefore, we use an indirection element and set
		 * present=false when there are no elements.
		 */
		if (node->ofTypename)
		{
			tmp_obj = new_objtree_for_type(relation->rd_rel->reloftype, -1);
			append_object_object(ret, "OF %{of_type}T", tmp_obj);
		}
		else
		{
			List	   *parents;
			ObjElem    *elem;

			parents = deparse_InhRelations(objectId);
			elem = (ObjElem *) linitial(parents);

			Assert(list_length(parents) == 1);

			append_format_string(ret, "PARTITION OF");

			append_object_object(ret, "%{parent_identity}D",
								 elem->value.object);
		}

		tableelts = deparse_TableElements(relation, node->tableElts, dpcontext,
										  true, /* typed table */
										  false);	/* not composite */
		tableelts = obtainConstraints(tableelts, objectId, InvalidOid);

		tmp_obj = new_objtree("");
		if (tableelts)
			append_array_object(tmp_obj, "(%{elements:, }s)", tableelts);
		else
			append_not_present(tmp_obj);

		append_object_object(ret, "%{table_elements}s", tmp_obj);
	}
	else
	{
		List	   *tableelts = NIL;

		/*
		 * There is no need to process LIKE clauses separately; they have
		 * already been transformed into columns and constraints.
		 */

		/*
		 * Process table elements: column definitions and constraints.  Only
		 * the column definitions are obtained from the parse node itself.  To
		 * get constraints we rely on pg_constraint, because the parse node
		 * might be missing some things such as the name of the constraints.
		 */
		tableelts = deparse_TableElements(relation, node->tableElts, dpcontext,
										  false,	/* not typed table */
										  false);	/* not composite */
		tableelts = obtainConstraints(tableelts, objectId, InvalidOid);

		if (tableelts)
			append_array_object(ret, "(%{table_elements:, }s)", tableelts);
		else
			append_format_string(ret, "()");

		/*
		 * Add inheritance specification.  We cannot simply scan the list of
		 * parents from the parser node, because that may lack the actual
		 * qualified names of the parent relations.  Rather than trying to
		 * re-resolve them from the information in the parse node, it seems
		 * more accurate and convenient to grab it from pg_inherits.
		 */
		tmp_obj = new_objtree("INHERITS");
		if (node->inhRelations != NIL)
			append_array_object(tmp_obj, "(%{parents:, }D)", deparse_InhRelations(objectId));
		else
		{
			append_null_object(tmp_obj, "(%{parents:, }D)");
			append_not_present(tmp_obj);
		}
		append_object_object(ret, "%{inherits}s", tmp_obj);
	}

	/* FOR VALUES clause */
	if (node->partbound)
	{
		/*
		 * Get pg_class.relpartbound. We cannot use partbound in the parsetree
		 * directly as it's the original partbound expression which haven't
		 * been transformed.
		 */
		append_string_object(ret, "%{partition_bound}s", "partition_bound",
							 RelationGetPartitionBound(objectId));
	}

	/* PARTITION BY clause */
	tmp_obj = new_objtree("PARTITION BY");
	if (relation->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		append_string_object(tmp_obj, "%{definition}s", "definition",
							 pg_get_partkeydef_string(objectId));
	else
	{
		append_null_object(tmp_obj, "%{definition}s");
		append_not_present(tmp_obj);
	}
	append_object_object(ret, "%{partition_by}s", tmp_obj);

	/* USING clause */
	tmp_obj = new_objtree("USING");
	if (node->accessMethod)
		append_string_object(tmp_obj, "%{access_method}I", "access_method",
							 node->accessMethod);
	else
	{
		append_null_object(tmp_obj, "%{access_method}I");
		append_not_present(tmp_obj);
	}
	append_object_object(ret, "%{access_method}s", tmp_obj);

	/* WITH clause */
	tmp_obj = new_objtree("WITH");

	foreach(cell, node->options)
	{
		ObjTree    *tmp_obj2;
		DefElem    *opt = (DefElem *) lfirst(cell);

		tmp_obj2 = deparse_DefElem(opt, false);
		list = lappend(list, new_object_object(tmp_obj2));
	}

	if (list)
		append_array_object(tmp_obj, "(%{with:, }s)", list);
	else
		append_not_present(tmp_obj);

	append_object_object(ret, "%{with_clause}s", tmp_obj);

	append_object_object(ret, "%{on_commit}s",
						 deparse_OnCommitClause(node->oncommit));

	tmp_obj = new_objtree("TABLESPACE");
	if (node->tablespacename)
		append_string_object(tmp_obj, "%{tablespace}I", "tablespace",
							 node->tablespacename);
	else
	{
		append_null_object(tmp_obj, "%{tablespace}I");
		append_not_present(tmp_obj);
	}
	append_object_object(ret, "%{tablespace}s", tmp_obj);

	relation_close(relation, AccessShareLock);

	return ret;
}

/*
 * Deparse CREATE TABLE AS command.
 *
 * deparse_CreateStmt do the actual work as we deparse the final CreateStmt for
 * CREATE TABLE AS command.
 */
static ObjTree *
deparse_CreateTableAsStmt(CollectedCommand *cmd)
{
	Oid			objectId;
	Node	   *parsetree;

	Assert(cmd->type == SCT_CreateTableAs);

	parsetree = cmd->d.ctas.real_create;
	objectId = cmd->d.ctas.address.objectId;

	return deparse_CreateStmt(objectId, parsetree);
}

/*
 * Deparse all the collected subcommands and return an ObjTree representing the
 * alter command.
 *
 * Verbose syntax
 * ALTER reltype %{identity}D %{subcmds:, }s
 */
static ObjTree *
deparse_AlterRelation(CollectedCommand *cmd)
{
	ObjTree    *ret;
	ObjTree    *tmp_obj;
	ObjTree    *tmp_obj2;
	List	   *dpcontext;
	Relation	rel;
	List	   *subcmds = NIL;
	ListCell   *cell;
	const char *reltype;
	bool		istype = false;
	List	   *exprs = NIL;
	Oid			relId = cmd->d.alterTable.objectId;
	AlterTableStmt *stmt = NULL;

	Assert(cmd->type == SCT_AlterTable);
	stmt = (AlterTableStmt *) cmd->parsetree;
	Assert(IsA(stmt, AlterTableStmt));

	/*
	 * ALTER TABLE subcommands generated for TableLikeClause is processed in
	 * the top level CREATE TABLE command; return empty here.
	 */
	if (stmt->table_like)
		return NULL;

	rel = relation_open(relId, AccessShareLock);
	dpcontext = deparse_context_for(RelationGetRelationName(rel),
									relId);

	switch (rel->rd_rel->relkind)
	{
		case RELKIND_RELATION:
		case RELKIND_PARTITIONED_TABLE:
			reltype = "TABLE";
			break;
		case RELKIND_INDEX:
		case RELKIND_PARTITIONED_INDEX:
			reltype = "INDEX";
			break;
		case RELKIND_VIEW:
			reltype = "VIEW";
			break;
		case RELKIND_COMPOSITE_TYPE:
			reltype = "TYPE";
			istype = true;
			break;
		case RELKIND_FOREIGN_TABLE:
			reltype = "FOREIGN TABLE";
			break;
		case RELKIND_MATVIEW:
			reltype = "MATERIALIZED VIEW";
			break;

			/* TODO support for partitioned table */

		default:
			elog(ERROR, "unexpected relkind %d", rel->rd_rel->relkind);
	}

	ret = new_objtree_VA("ALTER %{objtype}s %{identity}D", 2,
						 "objtype", ObjTypeString, reltype,
						 "identity", ObjTypeObject,
						 new_objtree_for_qualname(rel->rd_rel->relnamespace,
												  RelationGetRelationName(rel)));

	foreach(cell, cmd->d.alterTable.subcmds)
	{
		CollectedATSubcmd *sub = (CollectedATSubcmd *) lfirst(cell);
		AlterTableCmd *subcmd = (AlterTableCmd *) sub->parsetree;
		ObjTree    *tree;

		Assert(IsA(subcmd, AlterTableCmd));

	   /*
		* If the ALTER TABLE command for the parent table includes subcommands
		* for child table(s), do not deparse the subcommand for child
		* table(s).
		*/
		if (sub->address.objectId != relId &&
			has_superclass(sub->address.objectId))
			continue;

		switch (subcmd->subtype)
		{
			case AT_AddColumn:
				/* XXX need to set the "recurse" bit somewhere? */
				Assert(IsA(subcmd->def, ColumnDef));
				tree = deparse_ColumnDef(rel, dpcontext, false,
										 (ColumnDef *) subcmd->def, true, &exprs);
				tmp_obj = new_objtree_VA("ADD %{objtype}s %{if_not_exists}s %{definition}s", 4,
										"objtype", ObjTypeString,
										istype ? "ATTRIBUTE" : "COLUMN",
										"type", ObjTypeString, "add column",
										"if_not_exists", ObjTypeString,
										subcmd->missing_ok ? "IF NOT EXISTS" : "",
										"definition", ObjTypeObject, tree);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_AddIndexConstraint:
				{
					IndexStmt  *istmt;
					Relation	idx;
					Oid			constrOid = sub->address.objectId;

					Assert(IsA(subcmd->def, IndexStmt));
					istmt = (IndexStmt *) subcmd->def;

					Assert(istmt->isconstraint && istmt->unique);

					idx = relation_open(istmt->indexOid, AccessShareLock);

					/*
					 * Verbose syntax
					 *
					 * ADD CONSTRAINT %{name}I %{constraint_type}s USING INDEX
					 * %index_name}I %{deferrable}s %{init_deferred}s
					 */
					tmp_obj = new_objtree_VA("ADD CONSTRAINT %{name}I %{constraint_type}s USING INDEX %{index_name}I %{deferrable}s %{init_deferred}s", 6,
											"type", ObjTypeString, "add constraint using index",
											"name", ObjTypeString, get_constraint_name(constrOid),
											"constraint_type", ObjTypeString,
											istmt->primary ? "PRIMARY KEY" : "UNIQUE",
											"index_name", ObjTypeString,
											RelationGetRelationName(idx),
											"deferrable", ObjTypeString,
											istmt->deferrable ? "DEFERRABLE" : "NOT DEFERRABLE",
											"init_deferred", ObjTypeString,
											istmt->initdeferred ? "INITIALLY DEFERRED" : "INITIALLY IMMEDIATE");

					subcmds = lappend(subcmds, new_object_object(tmp_obj));

					relation_close(idx, AccessShareLock);
				}
				break;

			case AT_ReAddIndex:
			case AT_ReAddConstraint:
			case AT_ReAddDomainConstraint:
			case AT_ReAddComment:
			case AT_ReplaceRelOptions:
			case AT_CheckNotNull:
			case AT_ReAddStatistics:
				/* Subtypes used for internal operations; nothing to do here */
				break;

			case AT_CookedColumnDefault:
				{
					Relation	attrrel;
					HeapTuple	atttup;
					Form_pg_attribute attStruct;

					attrrel = table_open(AttributeRelationId, RowExclusiveLock);
					atttup = SearchSysCacheCopy2(ATTNUM,
												 ObjectIdGetDatum(RelationGetRelid(rel)),
												 Int16GetDatum(subcmd->num));
					if (!HeapTupleIsValid(atttup))
						elog(ERROR, "cache lookup failed for attribute %d of relation with OID %u",
							 subcmd->num, RelationGetRelid(rel));
					attStruct = (Form_pg_attribute) GETSTRUCT(atttup);

					/*
					 * Both default and generation expression not supported
					 * together.
					 */
					if (!attStruct->attgenerated)
						elog(WARNING, "unsupported alter table subtype %d",
							 subcmd->subtype);

					heap_freetuple(atttup);
					table_close(attrrel, RowExclusiveLock);
					break;
				}

			case AT_AddColumnToView:
				/* CREATE OR REPLACE VIEW -- nothing to do here */
				break;

			case AT_ColumnDefault:
				if (subcmd->def == NULL)
					tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I DROP DEFAULT", 2,
											"type", ObjTypeString, "drop default",
											"column", ObjTypeString, subcmd->name);
				else
				{
					List	   *dpcontext_rel;
					HeapTuple	attrtup;
					AttrNumber	attno;

					tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I SET DEFAULT", 2,
											"type", ObjTypeString, "set default",
											"column", ObjTypeString, subcmd->name);

					dpcontext_rel = deparse_context_for(RelationGetRelationName(rel),
														RelationGetRelid(rel));
					attrtup = SearchSysCacheAttName(RelationGetRelid(rel), subcmd->name);
					attno = ((Form_pg_attribute) GETSTRUCT(attrtup))->attnum;
					append_string_object(tmp_obj, "%{definition}s", "definition",
										 RelationGetColumnDefault(rel, attno,
																  dpcontext_rel,
																  NULL));
					ReleaseSysCache(attrtup);
				}

				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropNotNull:
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I DROP NOT NULL", 2,
										"type", ObjTypeString, "drop not null",
										"column", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_ForceRowSecurity:
				tmp_obj = new_objtree("FORCE ROW LEVEL SECURITY");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_NoForceRowSecurity:
				tmp_obj = new_objtree("NO FORCE ROW LEVEL SECURITY");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetNotNull:
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I SET NOT NULL", 2,
										"type", ObjTypeString, "set not null",
										"column", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropExpression:
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I DROP EXPRESSION %{if_exists}s", 3,
										"type", ObjTypeString, "drop expression",
										"column", ObjTypeString, subcmd->name,
										"if_exists", ObjTypeString,
										subcmd->missing_ok ? "IF EXISTS" : "");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetStatistics:
				{
					Assert(IsA(subcmd->def, Integer));
					if (subcmd->name)
						tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I SET STATISTICS %{statistics}n", 3,
												"type", ObjTypeString, "set statistics",
												"column", ObjTypeString, subcmd->name,
												"statistics", ObjTypeInteger,
												intVal((Integer *) subcmd->def));
					else
						tmp_obj = new_objtree_VA("ALTER COLUMN %{column}n SET STATISTICS %{statistics}n", 3,
												"type", ObjTypeString, "set statistics",
												"column", ObjTypeInteger, subcmd->num,
												"statistics", ObjTypeInteger,
												intVal((Integer *) subcmd->def));
					subcmds = lappend(subcmds, new_object_object(tmp_obj));
				}
				break;

			case AT_SetOptions:
			case AT_ResetOptions:
				subcmds = lappend(subcmds, new_object_object(
															 deparse_ColumnSetOptions(subcmd)));
				break;

			case AT_SetStorage:
				Assert(IsA(subcmd->def, String));
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I SET STORAGE %{storage}s", 3,
										"type", ObjTypeString, "set storage",
										"column", ObjTypeString, subcmd->name,
										"storage", ObjTypeString,
										strVal((String *) subcmd->def));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetCompression:
				Assert(IsA(subcmd->def, String));
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I SET COMPRESSION %{compression_method}s", 3,
										"type", ObjTypeString, "set compression",
										"column", ObjTypeString, subcmd->name,
										"compression_method", ObjTypeString,
										strVal((String *) subcmd->def));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropColumn:
				tmp_obj = new_objtree_VA("DROP %{objtype}s %{if_exists}s %{column}I", 4,
										"objtype", ObjTypeString,
										istype ? "ATTRIBUTE" : "COLUMN",
										"type", ObjTypeString, "drop column",
										"if_exists", ObjTypeString,
										subcmd->missing_ok ? "IF EXISTS" : "",
										"column", ObjTypeString, subcmd->name);
				tmp_obj2 = new_objtree_VA("CASCADE", 1,
										 "present", ObjTypeBool, subcmd->behavior);
				append_object_object(tmp_obj, "%{cascade}s", tmp_obj2);

				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_AddIndex:
				{
					Oid			idxOid = sub->address.objectId;
					IndexStmt  *istmt;
					Relation	idx;
					const char *idxname;
					Oid			constrOid;

					Assert(IsA(subcmd->def, IndexStmt));
					istmt = (IndexStmt *) subcmd->def;

					if (!istmt->isconstraint)
						break;

					idx = relation_open(idxOid, AccessShareLock);
					idxname = RelationGetRelationName(idx);

					constrOid = get_relation_constraint_oid(
															cmd->d.alterTable.objectId, idxname, false);

					tmp_obj = new_objtree_VA("ADD CONSTRAINT %{name}I %{definition}s", 3,
											"type", ObjTypeString, "add constraint",
											"name", ObjTypeString, idxname,
											"definition", ObjTypeString,
											pg_get_constraintdef_string(constrOid));
					subcmds = lappend(subcmds, new_object_object(tmp_obj));

					relation_close(idx, AccessShareLock);
				}
				break;

			case AT_AddConstraint:
				{
					/* XXX need to set the "recurse" bit somewhere? */
					Oid			constrOid = sub->address.objectId;
					bool		isnull;
					HeapTuple	tup;
					Datum		val;
					Constraint *constr;

					/* Skip adding constraint for inherits table sub command */
					if (!constrOid)
						continue;

					Assert(IsA(subcmd->def, Constraint));
					constr = castNode(Constraint, subcmd->def);

					if (!constr->skip_validation)
					{
						tup = SearchSysCache1(CONSTROID, ObjectIdGetDatum(constrOid));

						if (HeapTupleIsValid(tup))
						{
							char	   *conbin;

							/* Fetch constraint expression in parsetree form */
							val = SysCacheGetAttr(CONSTROID, tup,
												  Anum_pg_constraint_conbin, &isnull);

							if (!isnull)
							{
								conbin = TextDatumGetCString(val);
								exprs = lappend(exprs, stringToNode(conbin));
							}

							ReleaseSysCache(tup);
						}
					}

					tmp_obj = new_objtree_VA("ADD CONSTRAINT %{name}I %{definition}s", 3,
											"type", ObjTypeString, "add constraint",
											"name", ObjTypeString, get_constraint_name(constrOid),
											"definition", ObjTypeString,
											pg_get_constraintdef_string(constrOid));
					subcmds = lappend(subcmds, new_object_object(tmp_obj));
				}
				break;

			case AT_AlterConstraint:
				{
					Oid			constrOid = sub->address.objectId;
					Constraint *c = (Constraint *) subcmd->def;

					/* If no constraint was altered, silently skip it */
					if (!OidIsValid(constrOid))
						break;

					Assert(IsA(c, Constraint));
					tmp_obj = new_objtree_VA("ALTER CONSTRAINT %{name}I %{deferrable}s %{init_deferred}s", 4,
											"type", ObjTypeString, "alter constraint",
											"name", ObjTypeString, get_constraint_name(constrOid),
											"deferrable", ObjTypeString,
											c->deferrable ? "DEFERRABLE" : "NOT DEFERRABLE",
											"init_deferred", ObjTypeString,
											c->initdeferred ? "INITIALLY DEFERRED" : "INITIALLY IMMEDIATE");

					subcmds = lappend(subcmds, new_object_object(tmp_obj));
				}
				break;

			case AT_ValidateConstraint:
				tmp_obj = new_objtree_VA("VALIDATE CONSTRAINT %{constraint}I", 2,
										"type", ObjTypeString, "validate constraint",
										"constraint", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropConstraint:
				tmp_obj = new_objtree_VA("DROP CONSTRAINT %{if_exists}s %{constraint}I %{cascade}s", 4,
										"type", ObjTypeString, "drop constraint",
										"if_exists", ObjTypeString,
										subcmd->missing_ok ? "IF EXISTS" : "",
										"constraint", ObjTypeString, subcmd->name,
										"cascade", ObjTypeString,
										subcmd->behavior == DROP_CASCADE ? "CASCADE" : "");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_AlterColumnType:
				{
					TupleDesc	tupdesc = RelationGetDescr(rel);
					Form_pg_attribute att;
					ColumnDef  *def;

					att = &(tupdesc->attrs[sub->address.objectSubId - 1]);
					def = (ColumnDef *) subcmd->def;
					Assert(IsA(def, ColumnDef));

					/*
					 * Verbose syntax
					 *
					 * Composite types: ALTER reltype %{column}I SET DATA TYPE
					 * %{datatype}T %{collation}s ATTRIBUTE %{cascade}s
					 *
					 * Normal types: ALTER reltype %{column}I SET DATA TYPE
					 * %{datatype}T %{collation}s COLUMN %{using}s
					 */
					tmp_obj = new_objtree_VA("ALTER %{objtype}s %{column}I SET DATA TYPE %{datatype}T", 4,
											"objtype", ObjTypeString,
											istype ? "ATTRIBUTE" : "COLUMN",
											"type", ObjTypeString, "alter column type",
											"column", ObjTypeString, subcmd->name,
											"datatype", ObjTypeObject,
											new_objtree_for_type(att->atttypid,
																 att->atttypmod));

					/* Add a COLLATE clause, if needed */
					tmp_obj2 = new_objtree("COLLATE");
					if (OidIsValid(att->attcollation))
					{
						ObjTree    *collname;

						collname = new_objtree_for_qualname_id(CollationRelationId,
															   att->attcollation);
						append_object_object(tmp_obj2, "%{name}D", collname);
					}
					else
						append_not_present(tmp_obj2);
					append_object_object(tmp_obj, "%{collation}s", tmp_obj2);

					/* If not a composite type, add the USING clause */
					if (!istype)
					{
						/*
						 * If there's a USING clause, transformAlterTableStmt
						 * ran it through transformExpr and stored the
						 * resulting node in cooked_default, which we can use
						 * here.
						 */
						tmp_obj2 = new_objtree("USING");
						if (def->raw_default)
							append_string_object(tmp_obj2, "%{expression}s",
												 "expression",
												 sub->usingexpr);
						else
							append_not_present(tmp_obj2);
						append_object_object(tmp_obj, "%{using}s", tmp_obj2);
					}

					/* If it's a composite type, add the CASCADE clause */
					if (istype)
					{
						tmp_obj2 = new_objtree("CASCADE");
						if (subcmd->behavior != DROP_CASCADE)
							append_not_present(tmp_obj2);
						append_object_object(tmp_obj, "%{cascade}s", tmp_obj2);
					}

					subcmds = lappend(subcmds, new_object_object(tmp_obj));
				}
				break;

#ifdef TODOLIST
			case AT_AlterColumnGenericOptions:
				tmp_obj = deparse_FdwOptions((List *) subcmd->def,
											subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
#endif
			case AT_ChangeOwner:
				tmp_obj = new_objtree_VA("OWNER TO %{owner}I", 2,
										"type", ObjTypeString, "change owner",
										"owner", ObjTypeString,
										get_rolespec_name(subcmd->newowner));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_ClusterOn:
				tmp_obj = new_objtree_VA("CLUSTER ON %{index}I", 2,
										"type", ObjTypeString, "cluster on",
										"index", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropCluster:
				tmp_obj = new_objtree_VA("SET WITHOUT CLUSTER", 1,
										"type", ObjTypeString, "set without cluster");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetLogged:
				tmp_obj = new_objtree_VA("SET LOGGED", 1,
										"type", ObjTypeString, "set logged");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetUnLogged:
				tmp_obj = new_objtree_VA("SET UNLOGGED", 1,
										"type", ObjTypeString, "set unlogged");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropOids:
				tmp_obj = new_objtree_VA("SET WITHOUT OIDS", 1,
										"type", ObjTypeString, "set without oids");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			case AT_SetAccessMethod:
				tmp_obj = new_objtree_VA("SET ACCESS METHOD %{access_method}I", 2,
										"type", ObjTypeString, "set access method",
										"access_method", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			case AT_SetTableSpace:
				tmp_obj = new_objtree_VA("SET TABLESPACE %{tablespace}I", 2,
										"type", ObjTypeString, "set tablespace",
										"tablespace", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_SetRelOptions:
			case AT_ResetRelOptions:
				subcmds = lappend(subcmds, new_object_object(
															 deparse_RelSetOptions(subcmd)));
				break;

			case AT_EnableTrig:
				tmp_obj = new_objtree_VA("ENABLE TRIGGER %{trigger}I", 2,
										"type", ObjTypeString, "enable trigger",
										"trigger", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableAlwaysTrig:
				tmp_obj = new_objtree_VA("ENABLE ALWAYS TRIGGER %{trigger}I", 2,
										"type", ObjTypeString, "enable always trigger",
										"trigger", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableReplicaTrig:
				tmp_obj = new_objtree_VA("ENABLE REPLICA TRIGGER %{trigger}I", 2,
										"type", ObjTypeString, "enable replica trigger",
										"trigger", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DisableTrig:
				tmp_obj = new_objtree_VA("DISABLE TRIGGER %{trigger}I", 2,
										"type", ObjTypeString, "disable trigger",
										"trigger", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableTrigAll:
				tmp_obj = new_objtree_VA("ENABLE TRIGGER ALL", 1,
										"type", ObjTypeString, "enable trigger all");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DisableTrigAll:
				tmp_obj = new_objtree_VA("DISABLE TRIGGER ALL", 1,
										"type", ObjTypeString, "disable trigger all");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableTrigUser:
				tmp_obj = new_objtree_VA("ENABLE TRIGGER USER", 1,
										"type", ObjTypeString, "enable trigger user");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DisableTrigUser:
				tmp_obj = new_objtree_VA("DISABLE TRIGGER USER", 1,
										"type", ObjTypeString, "disable trigger user");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableRule:
				tmp_obj = new_objtree_VA("ENABLE RULE %{rule}I", 2,
										"type", ObjTypeString, "enable rule",
										"rule", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableAlwaysRule:
				tmp_obj = new_objtree_VA("ENABLE ALWAYS RULE %{rule}I", 2,
										"type", ObjTypeString, "enable always rule",
										"rule", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableReplicaRule:
				tmp_obj = new_objtree_VA("ENABLE REPLICA RULE %{rule}I", 2,
										"type", ObjTypeString, "enable replica rule",
										"rule", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DisableRule:
				tmp_obj = new_objtree_VA("DISABLE RULE %{rule}I", 2,
										"type", ObjTypeString, "disable rule",
										"rule", ObjTypeString, subcmd->name);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_AddInherit:
				tmp_obj = new_objtree_VA("INHERIT %{parent}D", 2,
										"type", ObjTypeString, "inherit",
										"parent", ObjTypeObject,
										new_objtree_for_qualname_id(RelationRelationId,
																	sub->address.objectId));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropInherit:
				tmp_obj = new_objtree_VA("NO INHERIT %{parent}D", 2,
										"type", ObjTypeString, "drop inherit",
										"parent", ObjTypeObject,
										new_objtree_for_qualname_id(RelationRelationId,
																	sub->address.objectId));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_AddOf:
				tmp_obj = new_objtree_VA("OF %{type_of}T", 2,
										"type", ObjTypeString, "add of",
										"type_of", ObjTypeObject,
										new_objtree_for_type(sub->address.objectId, -1));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DropOf:
				tmp_obj = new_objtree_VA("NOT OF", 1,
										"type", ObjTypeString, "not of");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_ReplicaIdentity:
				tmp_obj = new_objtree_VA("REPLICA IDENTITY", 1,
										"type", ObjTypeString, "replica identity");
				switch (((ReplicaIdentityStmt *) subcmd->def)->identity_type)
				{
					case REPLICA_IDENTITY_DEFAULT:
						append_string_object(tmp_obj, "%{ident}s", "ident",
											 "DEFAULT");
						break;
					case REPLICA_IDENTITY_FULL:
						append_string_object(tmp_obj, "%{ident}s", "ident",
											 "FULL");
						break;
					case REPLICA_IDENTITY_NOTHING:
						append_string_object(tmp_obj, "%{ident}s", "ident",
											 "NOTHING");
						break;
					case REPLICA_IDENTITY_INDEX:
						tmp_obj2 = new_objtree_VA("USING INDEX %{index}I", 1,
												 "index", ObjTypeString,
												 ((ReplicaIdentityStmt *) subcmd->def)->name);
						append_object_object(tmp_obj, "%{ident}s", tmp_obj2);
						break;
				}
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_EnableRowSecurity:
				tmp_obj = new_objtree_VA("ENABLE ROW LEVEL SECURITY", 1,
										"type", ObjTypeString, "enable row security");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;

			case AT_DisableRowSecurity:
				tmp_obj = new_objtree_VA("DISABLE ROW LEVEL SECURITY", 1,
										"type", ObjTypeString, "disable row security");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
#ifdef TODOLIST
			case AT_GenericOptions:
				tmp_obj = deparse_FdwOptions((List *) subcmd->def, NULL);
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
#endif
			case AT_AttachPartition:
				tmp_obj = new_objtree_VA("ATTACH PARTITION %{partition_identity}D", 2,
										"type", ObjTypeString, "attach partition",
										"partition_identity", ObjTypeObject,
										new_objtree_for_qualname_id(RelationRelationId,
																	sub->address.objectId));

				if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
					append_string_object(tmp_obj, "%{partition_bound}s",
										 "partition_bound",
										 RelationGetPartitionBound(sub->address.objectId));

				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			case AT_DetachPartition:
			{
				PartitionCmd *cmd;

				Assert(IsA(subcmd->def, PartitionCmd));
				cmd = (PartitionCmd *) subcmd->def;

				tmp_obj = new_objtree_VA("DETACH PARTITION %{partition_identity}D %{concurrent}s", 3,
										"type", ObjTypeString,
										"detach partition",
										"partition_identity", ObjTypeObject,
										new_objtree_for_qualname_id(RelationRelationId,
																	sub->address.objectId),
										cmd->concurrent ? "CONCURRENTLY" : "");
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			}
			case AT_DetachPartitionFinalize:
				tmp_obj = new_objtree_VA("DETACH PARTITION %{partition_identity}D FINALIZE", 2,
										"type", ObjTypeString, "detach partition finalize",
										"partition_identity", ObjTypeObject,
										new_objtree_for_qualname_id(RelationRelationId,
																	sub->address.objectId));
				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			case AT_AddIdentity:
				{
					AttrNumber	attnum;
					Oid			seq_relid;
					ObjTree    *seqdef;
					ColumnDef  *coldef = (ColumnDef *) subcmd->def;

					tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I", 2,
											"type", ObjTypeString, "add identity",
											"column", ObjTypeString, subcmd->name);

					attnum = get_attnum(RelationGetRelid(rel), subcmd->name);
					seq_relid = getIdentitySequence(RelationGetRelid(rel), attnum, true);

					if (OidIsValid(seq_relid))
					{
						seqdef = deparse_ColumnIdentity(seq_relid, coldef->identity, false);
						append_object_object(tmp_obj, "ADD %{identity_column}s", seqdef);
					}

					subcmds = lappend(subcmds, new_object_object(tmp_obj));
				}
				break;
			case AT_SetIdentity:
				{
					DefElem    *defel;
					char		identity = 0;
					ObjTree    *seqdef;
					AttrNumber	attnum;
					Oid			seq_relid;


					tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I", 2,
											"type", ObjTypeString, "set identity",
											"column", ObjTypeString, subcmd->name);

					if (subcmd->def)
					{
						List	   *def = (List *) subcmd->def;

						Assert(IsA(subcmd->def, List));

						defel = linitial_node(DefElem, def);
						identity = defGetInt32(defel);
					}

					attnum = get_attnum(RelationGetRelid(rel), subcmd->name);
					seq_relid = getIdentitySequence(RelationGetRelid(rel), attnum, true);

					if (OidIsValid(seq_relid))
					{
						seqdef = deparse_ColumnIdentity(seq_relid, identity, true);
						append_object_object(tmp_obj, "%{definition}s", seqdef);
					}

					subcmds = lappend(subcmds, new_object_object(tmp_obj));
					break;
				}
			case AT_DropIdentity:
				tmp_obj = new_objtree_VA("ALTER COLUMN %{column}I DROP IDENTITY", 2,
										"type", ObjTypeString, "drop identity",
										"column", ObjTypeString, subcmd->name);

				append_string_object(tmp_obj, "%{if_exists}s",
									 "if_exists",
									 subcmd->missing_ok ? "IF EXISTS" : "");

				subcmds = lappend(subcmds, new_object_object(tmp_obj));
				break;
			default:
				elog(WARNING, "unsupported alter table subtype %d",
					 subcmd->subtype);
				break;
		}

		/*
		 * We don't support replicating ALTER TABLE which contains volatile
		 * functions because It's possible the functions contain DDL/DML in
		 * which case these operations will be executed twice and cause
		 * duplicate data. In addition, we don't know whether the tables being
		 * accessed by these DDL/DML are published or not. So blindly allowing
		 * such functions can allow unintended clauses like the tables
		 * accessed in those functions may not even exist on the subscriber.
		 */
		if (contain_volatile_functions((Node *) exprs))
			elog(ERROR, "ALTER TABLE command using volatile function cannot be replicated");

		/*
		 * Clean the list as we already confirmed there is no volatile
		 * function.
		 */
		list_free(exprs);
		exprs = NIL;
	}

	table_close(rel, AccessShareLock);

	if (list_length(subcmds) == 0)
		return NULL;

	append_array_object(ret, "%{subcmds:, }s", subcmds);

	return ret;
}

/*
 * Handle deparsing of DROP commands.
 *
 * Verbose syntax
 * DROP %s IF EXISTS %%{objidentity}s %{cascade}s
 */
char *
deparse_drop_command(const char *objidentity, const char *objecttype,
					 DropBehavior behavior)
{
	StringInfoData str;
	char	   *command;
	char	   *identity = (char *) objidentity;
	ObjTree    *stmt;
	ObjTree    *tmp_obj;
	Jsonb	   *jsonb;

	initStringInfo(&str);

	stmt = new_objtree_VA("DROP %{objtype}s IF EXISTS %{objidentity}s", 2,
						  "objtype", ObjTypeString, objecttype,
						  "objidentity", ObjTypeString, identity);

	tmp_obj = new_objtree_VA("CASCADE", 1,
							"present", ObjTypeBool, behavior == DROP_CASCADE);
	append_object_object(stmt, "%{cascade}s", tmp_obj);

	jsonb = objtree_to_jsonb(stmt);
	command = JsonbToCString(&str, &jsonb->root, JSONB_ESTIMATED_LEN);

	return command;
}

/*
 * Handle deparsing of simple commands.
 *
 * This function should cover all cases handled in ProcessUtilitySlow.
 */
static ObjTree *
deparse_simple_command(CollectedCommand *cmd)
{
	Oid			objectId;
	Node	   *parsetree;

	Assert(cmd->type == SCT_Simple);

	parsetree = cmd->parsetree;
	objectId = cmd->d.simple.address.objectId;

	if (cmd->in_extension && (nodeTag(parsetree) != T_CreateExtensionStmt))
		return NULL;

	/* This switch needs to handle everything that ProcessUtilitySlow does */
	switch (nodeTag(parsetree))
	{
		case T_CreateSeqStmt:
			return deparse_CreateSeqStmt(objectId, parsetree);

		case T_CreateStmt:
			return deparse_CreateStmt(objectId, parsetree);

		case T_IndexStmt:
			return deparse_IndexStmt(objectId, parsetree);

		default:
			elog(LOG, "unrecognized node type in deparse command: %d",
				 (int) nodeTag(parsetree));
	}

	return NULL;
}

/*
 * Workhorse to deparse a CollectedCommand.
 */
char *
deparse_utility_command(CollectedCommand *cmd, bool verbose_mode)
{
	OverrideSearchPath *overridePath;
	MemoryContext oldcxt;
	MemoryContext tmpcxt;
	ObjTree    *tree;
	char	   *command = NULL;
	StringInfoData str;

	/*
	 * Allocate everything done by the deparsing routines into a temp context,
	 * to avoid having to sprinkle them with memory handling code, but
	 * allocate the output StringInfo before switching.
	 */
	initStringInfo(&str);
	tmpcxt = AllocSetContextCreate(CurrentMemoryContext,
								   "deparse ctx",
								   ALLOCSET_DEFAULT_MINSIZE,
								   ALLOCSET_DEFAULT_INITSIZE,
								   ALLOCSET_DEFAULT_MAXSIZE);
	oldcxt = MemoryContextSwitchTo(tmpcxt);

	/*
	 * Many routines underlying this one will invoke ruleutils.c functionality
	 * to obtain deparsed versions of expressions.  In such results, we want
	 * all object names to be qualified, so that results are "portable" to
	 * environments with different search_path settings.  Rather than inject
	 * what would be repetitive calls to override search path all over the
	 * place, we do it centrally here.
	 */
	overridePath = GetOverrideSearchPath(CurrentMemoryContext);
	overridePath->schemas = NIL;
	overridePath->addCatalog = false;
	overridePath->addTemp = true;
	PushOverrideSearchPath(overridePath);

	verbose = verbose_mode;

	switch (cmd->type)
	{
		case SCT_Simple:
			tree = deparse_simple_command(cmd);
			break;
		case SCT_AlterTable:
			tree = deparse_AlterRelation(cmd);
			break;
		case SCT_CreateTableAs:
			tree = deparse_CreateTableAsStmt(cmd);
			break;
		default:
			elog(ERROR, "unexpected deparse node type %d", cmd->type);
	}

	PopOverrideSearchPath();

	if (tree)
	{
		Jsonb	   *jsonb;

		jsonb = objtree_to_jsonb(tree);
		command = JsonbToCString(&str, &jsonb->root, JSONB_ESTIMATED_LEN);
	}

	/*
	 * Clean up.  Note that since we created the StringInfo in the caller's
	 * context, the output string is not deleted here.
	 */
	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(tmpcxt);

	return command;
}

/*
 * Given a CollectedCommand, return a JSON representation of it.
 *
 * The command is expanded fully so that there are no ambiguities even in the
 * face of search_path changes.
 */
Datum
ddl_deparse_to_json(PG_FUNCTION_ARGS)
{
	CollectedCommand *cmd = (CollectedCommand *) PG_GETARG_POINTER(0);
	char	   *command;

	command = deparse_utility_command(cmd, true);

	if (command)
		PG_RETURN_TEXT_P(cstring_to_text(command));

	PG_RETURN_NULL();
}
