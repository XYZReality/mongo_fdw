/*-------------------------------------------------------------------------
 *
 * deparse.c
 * 		Query deparser for mongo_fdw
 *
 * Portions Copyright (c) 2012-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 2004-2026, EnterpriseDB Corporation.
 * Portions Copyright (c) 2012–2014 Citus Data, Inc.
 *
 * IDENTIFICATION
 * 		deparse.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "mongo_wrapper.h"

#include <bson.h>
#include <json.h>

#include "access/htup_details.h"
#include "catalog/pg_operator.h"
#include "common/hashfn.h"
#include "executor/executor.h"
#include "mongoc.h"
#include "mongo_query.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "parser/parsetree.h"
#include "utils/rel.h"
#include "utils/syscache.h"

/*
 * Functions to gather information related to columns involved in the given
 * query, which is useful at the time of execution to prepare MongoDB query.
 */
static void mongo_check_op_expr(OpExpr *node, MongoRelQualInfo *qual_info);
static void mongo_check_var(Var *column, MongoRelQualInfo *qual_info);

/* Helper functions to form MongoDB query document. */
static void mongo_append_bool_expr(BoolExpr *node, BSON *queryDoc,
								   pipeline_cxt *context);
static void mongo_append_op_expr(OpExpr *node, BSON *child,
								 pipeline_cxt *context);

/*
 * mongo_check_qual
 *		Check the given qual expression and find the columns used in it.  We
 *		recursively traverse until we get a Var node and then retrieve the
 *		required information from it.
 */
void
mongo_check_qual(Expr *node, MongoRelQualInfo *qual_info)
{
	if (node == NULL)
		return;

	switch (nodeTag(node))
	{
		case T_Var:
			mongo_check_var((Var *) node, qual_info);
			break;
		case T_OpExpr:
			mongo_check_op_expr((OpExpr *) node, qual_info);
			break;
		case T_List:
			{
				ListCell   *lc;

				foreach(lc, (List *) node)
					mongo_check_qual((Expr *) lfirst(lc), qual_info);
			}
			break;
		case T_RelabelType:
			mongo_check_qual(((RelabelType *) node)->arg, qual_info);
			break;
		case T_BoolExpr:
			mongo_check_qual((Expr *) ((BoolExpr *) node)->args, qual_info);
			break;
		case T_Aggref:
			{
				ListCell   *lc;
				char	   *func_name = get_func_name(((Aggref *) node)->aggfnoid);

				/* Save aggregation operation name */
				qual_info->aggTypeList = lappend(qual_info->aggTypeList,
												 makeString(func_name));

				qual_info->is_agg_column = true;

				/* Save information whether this is a HAVING clause or not */
				if (qual_info->is_having)
					qual_info->isHavingList = lappend_int(qual_info->isHavingList,
														  true);
				else
					qual_info->isHavingList = lappend_int(qual_info->isHavingList,
														  false);

				/*
				 * The aggregation over '*' doesn't need column information.
				 * Hence, only to maintain the length of column information
				 * lists add dummy members into it.
				 *
				 * For aggregation over the column, add required information
				 * into the column information lists.
				 */
				if (((Aggref *) node)->aggstar)
				{
					qual_info->colNameList = lappend(qual_info->colNameList,
													 makeString("*"));
					qual_info->colNumList = lappend_int(qual_info->colNumList,
														0);
					qual_info->rtiList = lappend_int(qual_info->rtiList, 0);
					qual_info->isOuterList = lappend_int(qual_info->isOuterList,
														 0);
					/* Append dummy var */
					qual_info->aggColList = lappend(qual_info->aggColList,
													makeVar(0, 0, 0, 0, 0, 0));
					qual_info->is_agg_column = false;
				}
				else
				{
					foreach(lc, ((Aggref *) node)->args)
					{
						Node	   *n = (Node *) lfirst(lc);

						/* If TargetEntry, extract the expression from it */
						if (IsA(n, TargetEntry))
						{
							TargetEntry *tle = (TargetEntry *) n;

							n = (Node *) tle->expr;
						}

						mongo_check_qual((Expr *) n, qual_info);
					}
				}
			}
			break;
		case T_Const:
		case T_Param:
			/* Nothing to do here because we are looking only for Var's */
			break;
		default:
			elog(ERROR, "unsupported expression type to check: %d",
				 (int) nodeTag(node));
			break;
	}
}

/*
 * mongo_check_op_expr
 *		Check given operator expression.
 */
static void
mongo_check_op_expr(OpExpr *node, MongoRelQualInfo *qual_info)
{
	HeapTuple	tuple;
	Form_pg_operator form;
	char		oprkind;
	ListCell   *arg;

	/* Retrieve information about the operator from the system catalog. */
	tuple = SearchSysCache1(OPEROID, ObjectIdGetDatum(node->opno));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for operator %u", node->opno);

	form = (Form_pg_operator) GETSTRUCT(tuple);
	oprkind = form->oprkind;

	/* Sanity check. */
	Assert((oprkind == 'r' && list_length(node->args) == 1) ||
		   (oprkind == 'l' && list_length(node->args) == 1) ||
		   (oprkind == 'b' && list_length(node->args) == 2));

	/* Deparse left operand. */
	if (oprkind == 'r' || oprkind == 'b')
	{
		arg = list_head(node->args);
		mongo_check_qual(lfirst(arg), qual_info);
	}

	/* Deparse right operand. */
	if (oprkind == 'l' || oprkind == 'b')
	{
		arg = list_tail(node->args);
		mongo_check_qual(lfirst(arg), qual_info);
	}

	ReleaseSysCache(tuple);
}

/*
 * mongo_check_var
 *		Check the given Var and append required information related to columns
 *		involved in qual clauses to separate lists in context. Prepare separate
 *		list for aggregated columns directly (not related information).
 *
 * Save required information in the form of a list in MongoRelQualInfo
 * structure.  Prepare a hash table to avoid duplication of entry if one column
 * is involved in the multiple qual expressions.
 */
static void
mongo_check_var(Var *column, MongoRelQualInfo *qual_info)
{
	RangeTblEntry *rte;
	char	   *colname;
	ColumnHashKey key;
	bool		found;
	bool		is_outerrel = false;

	if (!(bms_is_member(column->varno, qual_info->foreignRel->relids) &&
		  column->varlevelsup == 0))
		return;					/* Var does not belong to foreign table */

	Assert(!IS_SPECIAL_VARNO(column->varno));

	if (!qual_info->exprColHash)
	{
		HASHCTL		hashInfo;

		memset(&hashInfo, 0, sizeof(hashInfo));
		hashInfo.keysize = sizeof(ColumnHashKey);
		hashInfo.entrysize = sizeof(ColumnHashKey);
		hashInfo.hcxt = CurrentMemoryContext;

		qual_info->exprColHash = hash_create("Join Expression Column Hash",
											 MaxHashTableSize,
											 &hashInfo,
											 (HASH_ELEM | HASH_BLOBS | HASH_CONTEXT));
	}

	key.varno = column->varno;
	key.varattno = column->varattno;

	hash_search(qual_info->exprColHash, (void *) &key, HASH_ENTER, &found);

	/*
	 * Add aggregated column in the aggColList even if it's already available
	 * in the hash table.  This is because multiple aggregation operations can
	 * be done on the same column.  So, to maintain the same length of
	 * aggregation functions and their columns, add each aggregation column.
	 */
	if (qual_info->is_agg_column)
	{
		qual_info->aggColList = lappend(qual_info->aggColList, column);
		qual_info->is_agg_column = false;
		if (found)
			return;
	}

	/*
	 * Don't add the duplicate column.  The Aggregated column is already taken
	 * care of.
	 */
	if (found)
		return;

	/* Get RangeTblEntry from array in PlannerInfo. */
	rte = planner_rt_fetch(column->varno, qual_info->root);

	colname = get_attname(rte->relid, column->varattno, false);

	/* Is relation inner or outer? */
	if (bms_is_member(column->varno, qual_info->outerRelids))
		is_outerrel = true;

	/* Fill the lists with elements */
	qual_info->colNameList = lappend(qual_info->colNameList, makeString(colname));
	qual_info->colNumList = lappend_int(qual_info->colNumList, column->varattno);
	qual_info->rtiList = lappend_int(qual_info->rtiList, column->varno);
	qual_info->isOuterList = lappend_int(qual_info->isOuterList, is_outerrel);
}

/*
 * mongo_get_jointype_name
 * 		Output join name for given join type
 */
const char *
mongo_get_jointype_name(JoinType jointype)
{
	switch (jointype)
	{
		case JOIN_INNER:
			return "INNER";

		case JOIN_LEFT:
			return "LEFT";

		case JOIN_RIGHT:
			return "RIGHT";

		default:
			/* Shouldn't come here, but protect from buggy code. */
			elog(ERROR, "unsupported join type %d", jointype);
	}

	/* Keep compiler happy */
	return NULL;
}

/*
 * mongo_field_path
 *		Return the MongoDB field path ("$name") referencing the given column
 *		in the expression being deparsed.
 */
static char *
mongo_field_path(Var *column, pipeline_cxt *context)
{
	bool		found = false;
	ColInfoHashKey key;
	ColInfoHashEntry *columnInfo;

	key.varNo = column->varno;
	key.varAttno = column->varattno;

	columnInfo = (ColInfoHashEntry *) hash_search(context->colInfoHash,
												  (void *) &key,
												  HASH_FIND,
												  &found);
	if (!found)
		elog(ERROR, "could not find column %d of relation %d in pushed-down expression",
			 column->varattno, column->varno);

	/*
	 * In the HAVING clause, columns can only refer to GROUP BY columns, which
	 * are the fields of the group key.
	 */
	if (context->isHaving)
		return psprintf("$_id.%s", columnInfo->colName);

	if (columnInfo->isOuter && context->isJoinClause)
		return psprintf("$$%s",
						get_varname_for_outer_col(columnInfo->colName));

	return psprintf("$%s", columnInfo->colName);
}

/*
 * mongo_append_string_literal
 *		Append a string constant to an expression.
 *
 * Strings starting with "$" would be taken for field paths or variables, so
 * wrap those in $literal.
 */
static void
mongo_append_string_literal(BSON *doc, const char *key, char *str)
{
	if (str[0] == '$')
	{
		BSON		literal;

		bsonAppendStartObject(doc, (char *) key, &literal);
		bsonAppendUTF8(&literal, "$literal", str);
		bsonAppendFinishObject(doc, &literal);
	}
	else
		bsonAppendUTF8(doc, key, str);
}

/*
 * mongo_eval_value
 *		Evaluate a Const or Param node.
 */
static Datum
mongo_eval_value(Expr *node, pipeline_cxt *context, bool *isnull)
{
	ExprState  *exprstate;

	if (IsA(node, Const))
	{
		*isnull = ((Const *) node)->constisnull;
		return ((Const *) node)->constvalue;
	}

	Assert(IsA(node, Param));
	exprstate = ExecInitExpr(node, (PlanState *) context->scanStateNode);

	return ExecEvalExpr(exprstate,
						context->scanStateNode->ss.ps.ps_ExprContext,
						isnull);
}

/*
 * mongo_expr_has_null_value
 *		Does any constant or parameter in the given operator expression tree
 *		evaluate to NULL?
 *
 * All the operators we push down are strict, so such an expression is NULL,
 * whereas MongoDB happily compares with null (e.g. {$lt: [1, null]} is false
 * but {$gt: [1, null]} is true).
 */
static bool
mongo_expr_has_null_value(Node *node, pipeline_cxt *context)
{
	ListCell   *lc;
	bool		isnull;

	if (node == NULL)
		return false;

	switch (nodeTag(node))
	{
		case T_Const:
		case T_Param:
			(void) mongo_eval_value((Expr *) node, context, &isnull);
			return isnull;
		case T_RelabelType:
			return mongo_expr_has_null_value((Node *) ((RelabelType *) node)->arg,
											 context);
		case T_OpExpr:
			foreach(lc, ((OpExpr *) node)->args)
			{
				if (mongo_expr_has_null_value(lfirst(lc), context))
					return true;
			}
			return false;
		default:
			return false;
	}
}

/*
 * mongo_append_not_null
 *		Append {key: {$ne: [field, null]}}.
 */
static void
mongo_append_not_null(BSON *doc, const char *key, const char *field)
{
	BSON		expr;
	BSON		args;

	bsonAppendStartObject(doc, (char *) key, &expr);
	bsonAppendStartArray(&expr, "$ne", &args);
	bsonAppendUTF8(&args, "0", (char *) field);
	bsonAppendNull(&args, "1");
	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(doc, &expr);
}

/*
 * mongo_append_value_checks
 *		Append to the array 'arr', starting at index 'index', a check for each
 *		column and aggregate in 'expr' that it has a non-NULL value of the
 *		expected type.
 *
 * PostgreSQL evaluates a comparison involving a NULL to NULL, i.e. not true,
 * so rows where any of these checks fails must not match.  See
 * mongo_append_type_check().
 *
 * 'aggIndex' is the number of the first HAVING aggregate in 'expr'.
 */
static void
mongo_append_value_checks(Node *expr, BSON *arr, int index, uint32 aggIndex,
						  pipeline_cxt *context)
{
	List	   *vars;
	ListCell   *lc;

	vars = pull_var_clause(expr, PVC_INCLUDE_AGGREGATES |
						   PVC_RECURSE_PLACEHOLDERS);

	foreach(lc, vars)
	{
		Node	   *node = (Node *) lfirst(lc);
		char	   *key = psprintf("%d", index++);

		if (IsA(node, Aggref))
			mongo_append_not_null(arr, key, psprintf("$%s%d", HAVING_RESULT_KEY,
													 aggIndex++));
		else if (context->isHaving)
		{
			/* The group keys are already NULL unless of the right type */
			mongo_append_not_null(arr, key,
								  mongo_field_path((Var *) node, context));
		}
		else
			mongo_append_type_check(arr, key,
									mongo_field_path((Var *) node, context),
									((Var *) node)->vartype);
	}
}

/*
 * mongo_count_aggrefs
 *		Count the aggregates in the given expression.
 */
static int
mongo_count_aggrefs(Node *expr)
{
	List	   *vars;
	ListCell   *lc;
	int			count = 0;

	vars = pull_var_clause(expr, PVC_INCLUDE_AGGREGATES |
						   PVC_RECURSE_PLACEHOLDERS);
	foreach(lc, vars)
	{
		if (IsA(lfirst(lc), Aggref))
			count++;
	}

	return count;
}

/*
 * mongo_append_expr
 *		Append given expression node.
 */
void
mongo_append_expr(Expr *node, BSON *child_doc, pipeline_cxt *context)
{
	char	   *key;

	if (node == NULL)
		return;

	key = psprintf("%d", context->arrayIndex);

	switch (nodeTag(node))
	{
		case T_Var:
			bsonAppendUTF8(child_doc, key,
						   mongo_field_path((Var *) node, context));
			break;
		case T_Const:
			append_constant_value(child_doc, key, (Const *) node);
			break;
		case T_OpExpr:
			mongo_append_op_expr((OpExpr *) node, child_doc, context);
			break;
		case T_RelabelType:
			mongo_append_expr(((RelabelType *) node)->arg, child_doc, context);
			break;
		case T_BoolExpr:
			mongo_append_bool_expr((BoolExpr *) node, child_doc, context);
			break;
		case T_Param:
			append_param_value(child_doc, key, (Param *) node,
							   context->scanStateNode);
			break;
		case T_Aggref:

			/*
			 * Aggregates only appear in the HAVING clause, where they refer
			 * to the results computed by the $group stage.  These are
			 * numbered in the order we visit them, which is the order in
			 * which mongo_check_qual() collected them for the $group stage.
			 */
			bsonAppendUTF8(child_doc, key,
						   psprintf("$%s%d", HAVING_RESULT_KEY,
									context->havingAggIndex++));
			break;
		default:
			elog(ERROR, "unsupported expression type to append: %d",
				 (int) nodeTag(node));
			break;
	}
}

/*
 * mongo_append_bool_var_test
 *		Append a test that the given boolean column is true (or false).
 *
 * {$eq: [field, true]} is true only for an actual boolean true, just as a
 * NULL isn't true (nor false) in PostgreSQL.
 */
static void
mongo_append_bool_var_test(Var *column, bool value, BSON *child_doc,
						   pipeline_cxt *context)
{
	BSON		expr;
	BSON		args;

	bsonAppendStartObject(child_doc, psprintf("%d", context->arrayIndex),
						  &expr);
	bsonAppendStartArray(&expr, "$eq", &args);
	bsonAppendUTF8(&args, "0", mongo_field_path(column, context));
	bsonAppendBool(&args, "1", value);
	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(child_doc, &expr);
}

/*
 * mongo_append_clause
 *		Append a boolean clause, i.e. a WHERE, JOIN or HAVING condition or an
 *		argument of AND, OR, or NOT.
 */
void
mongo_append_clause(Expr *node, BSON *child_doc, pipeline_cxt *context)
{
	Expr	   *arg = node;

	while (IsA(arg, RelabelType))
		arg = ((RelabelType *) arg)->arg;

	/* A boolean column by itself */
	if (IsA(arg, Var))
		mongo_append_bool_var_test((Var *) arg, true, child_doc, context);
	else
		mongo_append_expr(node, child_doc, context);
}

/*
 * mongo_append_bool_expr
 *		Recurse through a BoolExpr node to form MongoDB query pipeline.
 */
static void
mongo_append_bool_expr(BoolExpr *node, BSON *child_doc, pipeline_cxt *context)
{
	BSON		child;
	BSON		expr;
	ListCell   *lc;
	int			saved_array_index = context->arrayIndex;

	bsonAppendStartObject(child_doc, psprintf("%d", saved_array_index), &expr);

	if (node->boolop == NOT_EXPR)
	{
		Expr	   *arg = (Expr *) linitial(node->args);
		uint32		aggIndex = context->havingAggIndex;
		BSON		not_expr;
		BSON		not_arg;

		while (IsA(arg, RelabelType))
			arg = ((RelabelType *) arg)->arg;

		/* Negated boolean column */
		if (IsA(arg, Var))
		{
			BSON		args;

			bsonAppendStartArray(&expr, "$eq", &args);
			bsonAppendUTF8(&args, "0", mongo_field_path((Var *) arg, context));
			bsonAppendBool(&args, "1", false);
			bsonAppendFinishArray(&expr, &args);
			bsonAppendFinishObject(child_doc, &expr);
			return;
		}

		/*
		 * NOT is true in PostgreSQL only if its argument is false rather than
		 * NULL.  Ensure that by also requiring all the columns involved to
		 * have non-NULL values:
		 *
		 * {$and: [{$not: [<arg>]}, <value checks>]}
		 */
		bsonAppendStartArray(&expr, "$and", &child);
		bsonAppendStartObject(&child, "0", &not_expr);
		bsonAppendStartArray(&not_expr, "$not", &not_arg);
		context->arrayIndex = 0;
		mongo_append_clause(arg, &not_arg, context);
		bsonAppendFinishArray(&not_expr, &not_arg);
		bsonAppendFinishObject(&child, &not_expr);
		mongo_append_value_checks((Node *) arg, &child, 1, aggIndex, context);
	}
	else
	{
		bsonAppendStartArray(&expr, node->boolop == AND_EXPR ? "$and" : "$or",
							 &child);

		/* Reset to zero to be used for nested arrays */
		context->arrayIndex = 0;
		foreach(lc, node->args)
		{
			mongo_append_clause((Expr *) lfirst(lc), &child, context);
			context->arrayIndex++;
		}
	}

	bsonAppendFinishArray(&expr, &child);
	bsonAppendFinishObject(child_doc, &expr);

	/* Retain array index */
	context->arrayIndex = saved_array_index;
}

/*
 * mongo_append_plain_op
 *		Append {key: {<operator>: [<arguments>]}} for the given operator
 *		expression.
 */
static void
mongo_append_plain_op(OpExpr *node, BSON *child_doc, const char *key,
					  pipeline_cxt *context)
{
	BSON		expr;
	BSON		args;
	ListCell   *lc;
	int			saved_array_index = context->arrayIndex;

	context->opExprCount++;

	bsonAppendStartObject(child_doc, (char *) key, &expr);
	bsonAppendStartArray(&expr, mongo_operator_name(get_opname(node->opno)),
						 &args);

	/* Reset to zero to be used for nested arrays */
	context->arrayIndex = 0;
	foreach(lc, node->args)
	{
		mongo_append_expr((Expr *) lfirst(lc), &args, context);
		context->arrayIndex++;
	}

	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(child_doc, &expr);

	context->opExprCount--;
	context->arrayIndex = saved_array_index;
}

/*
 * mongo_append_cmp
 *		Append {key: {op: [field, value]}}, where the value is either the
 *		string 'str' or the ObjectId 'oid', and the field is converted to a
 *		string first if 'field_to_string'.
 */
static void
mongo_append_cmp(BSON *doc, const char *key, const char *op,
				 const char *field, bool field_to_string, char *str,
				 bson_oid_t *oid)
{
	BSON		expr;
	BSON		args;

	bsonAppendStartObject(doc, (char *) key, &expr);
	bsonAppendStartArray(&expr, op, &args);
	if (field_to_string)
	{
		BSON		to_string;

		bsonAppendStartObject(&args, "0", &to_string);
		bsonAppendUTF8(&to_string, "$toString", (char *) field);
		bsonAppendFinishObject(&args, &to_string);
	}
	else
		bsonAppendUTF8(&args, "0", (char *) field);
	if (oid)
		bsonAppendOid(&args, "1", oid);
	else
		mongo_append_string_literal(&args, "1", str);
	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(doc, &expr);
}

/*
 * mongo_append_type_is
 *		Append {key: {$eq: [{$type: field}, type_name]}}.
 */
static void
mongo_append_type_is(BSON *doc, const char *key, const char *field,
					 char *type_name)
{
	BSON		expr;
	BSON		args;
	BSON		type_expr;

	bsonAppendStartObject(doc, (char *) key, &expr);
	bsonAppendStartArray(&expr, "$eq", &args);
	bsonAppendStartObject(&args, "0", &type_expr);
	bsonAppendUTF8(&type_expr, "$type", (char *) field);
	bsonAppendFinishObject(&args, &type_expr);
	bsonAppendUTF8(&args, "1", type_name);
	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(doc, &expr);
}

/*
 * mongo_append_string_comparison
 *		Append a comparison between a string column and a string value, if
 *		that's what the given operator expression is.  Returns false if not.
 *
 * mongo_fdw presents ObjectIds as their hex strings, and a string column may
 * hold both strings and ObjectIds (e.g. "_id").  A value that is an ObjectId
 * string is therefore compared both as a string and as an ObjectId, which is
 * what makes e.g. _id = '62b597048a7fca1c83fc4eea' match an ObjectId, even
 * when the value is a text parameter.  Equality stays indexable:
 *
 *		{$or: [{$eq: [F, "62b5..."]}, {$eq: [F, ObjectId("62b5...")]}]}
 *
 * Ordering comparisons (only pushed down under the "C" collation) compare
 * each BSON type separately, because MongoDB orders all ObjectIds after all
 * strings:
 *
 *		{$or: [{$and: [{$eq: [{$type: F}, "string"]}, {$lt: [F, "abc"]}]},
 *			   {$and: [{$eq: [{$type: F}, "objectId"]},
 *					   {$lt: [{$toString: F}, "abc"]}]}]}
 *
 * (An ObjectId compares like its hex string, so for an ObjectId value the
 * second branch compares with the ObjectId instead, to be indexable.)
 */
static bool
mongo_append_string_comparison(OpExpr *node, BSON *child_doc,
							   const char *key, pipeline_cxt *context)
{
	Expr	   *larg;
	Expr	   *rarg;
	Var		   *column;
	Expr	   *value;
	const char *op;
	char	   *field;
	char	   *str;
	Datum		datum;
	bool		isnull;
	Oid			typoutput;
	bool		typisvarlena;
	bson_oid_t	oid;
	bool		is_oid;
	BSON		expr;
	BSON		args;

	if (list_length(node->args) != 2 ||
		mongo_type_class(exprType(linitial(node->args))) != MONGO_TYPE_STRING)
		return false;

	larg = (Expr *) linitial(node->args);
	rarg = (Expr *) lsecond(node->args);
	while (IsA(larg, RelabelType))
		larg = ((RelabelType *) larg)->arg;
	while (IsA(rarg, RelabelType))
		rarg = ((RelabelType *) rarg)->arg;

	op = mongo_operator_name(get_opname(node->opno));
	if (IsA(larg, Var) && (IsA(rarg, Const) || IsA(rarg, Param)))
	{
		column = (Var *) larg;
		value = rarg;
	}
	else if (IsA(rarg, Var) && (IsA(larg, Const) || IsA(larg, Param)))
	{
		column = (Var *) rarg;
		value = larg;

		/* Put the column on the left */
		if (strcmp(op, "$lt") == 0)
			op = "$gt";
		else if (strcmp(op, "$gt") == 0)
			op = "$lt";
		else if (strcmp(op, "$lte") == 0)
			op = "$gte";
		else if (strcmp(op, "$gte") == 0)
			op = "$lte";
	}
	else
		return false;

	field = mongo_field_path(column, context);

	/* The caller has checked that the value isn't NULL */
	datum = mongo_eval_value(value, context, &isnull);
	Assert(!isnull);
	getTypeOutputInfo(exprType((Node *) value), &typoutput, &typisvarlena);
	str = OidOutputFunctionCall(typoutput, datum);
	is_oid = mongo_parse_objectid(str, &oid);

	if (strcmp(op, "$eq") == 0)
	{
		/* Equal to the string, or to the ObjectId */
		if (!is_oid)
		{
			mongo_append_cmp(child_doc, key, op, field, false, str, NULL);
			return true;
		}

		bsonAppendStartObject(child_doc, (char *) key, &expr);
		bsonAppendStartArray(&expr, "$or", &args);
		mongo_append_cmp(&args, "0", op, field, false, str, NULL);
		mongo_append_cmp(&args, "1", op, field, false, NULL, &oid);
	}
	else if (strcmp(op, "$ne") == 0)
	{
		/* A non-NULL string or ObjectId, different from both */
		bsonAppendStartObject(child_doc, (char *) key, &expr);
		bsonAppendStartArray(&expr, "$and", &args);
		mongo_append_cmp(&args, "0", op, field, false, str, NULL);
		if (is_oid)
			mongo_append_cmp(&args, "1", op, field, false, NULL, &oid);
		mongo_append_type_check(&args, is_oid ? "2" : "1", field,
								column->vartype);
	}
	else
	{
		BSON		branch;
		BSON		branch_args;

		bsonAppendStartObject(child_doc, (char *) key, &expr);
		bsonAppendStartArray(&expr, "$or", &args);

		/* Strings */
		bsonAppendStartObject(&args, "0", &branch);
		bsonAppendStartArray(&branch, "$and", &branch_args);
		mongo_append_type_is(&branch_args, "0", field, "string");
		mongo_append_cmp(&branch_args, "1", op, field, false, str, NULL);
		bsonAppendFinishArray(&branch, &branch_args);
		bsonAppendFinishObject(&args, &branch);

		/* ObjectIds */
		bsonAppendStartObject(&args, "1", &branch);
		bsonAppendStartArray(&branch, "$and", &branch_args);
		mongo_append_type_is(&branch_args, "0", field, "objectId");
		if (is_oid)
			mongo_append_cmp(&branch_args, "1", op, field, false, NULL, &oid);
		else
			mongo_append_cmp(&branch_args, "1", op, field, true, str, NULL);
		bsonAppendFinishArray(&branch, &branch_args);
		bsonAppendFinishObject(&args, &branch);
	}

	bsonAppendFinishArray(&expr, &args);
	bsonAppendFinishObject(child_doc, &expr);

	return true;
}

/*
 * mongo_append_op_expr
 *		Deparse given operator expression.
 *
 * Operators nested in another operator are appended as they are, e.g.
 * {"$mod": ["$age", 2]}.  A top-level comparison additionally checks that
 * each column involved has a non-NULL value of the expected type:
 *
 *		{"$and": [{"$eq": [{"$mod": ["$age", 2]}, 1]},
 *				  {"$in": [{"$type": "$age"}, ["int", "long", "double"]]}]}
 *
 * In MongoDB, (null = null) and (null < 1) are true, a missing field isn't
 * even equal to null, and values of different types compare by type (e.g.
 * "10" > 5), while PostgreSQL sees all of these as NULL, which doesn't match.
 */
static void
mongo_append_op_expr(OpExpr *node, BSON *child_doc, pipeline_cxt *context)
{
	char	   *key = psprintf("%d", context->arrayIndex);
	uint32		aggIndex = context->havingAggIndex;
	BSON		expr;
	BSON		and_op;

	if (context->opExprCount > 0)
	{
		mongo_append_plain_op(node, child_doc, key, context);
		return;
	}

	/* The operators are strict, so a NULL value means no match */
	if (mongo_expr_has_null_value((Node *) node, context))
	{
		bsonAppendBool(child_doc, key, false);

		/* Skip the HAVING aggregates this would have referenced */
		context->havingAggIndex += mongo_count_aggrefs((Node *) node);
		return;
	}

	if (mongo_append_string_comparison(node, child_doc, key, context))
		return;

	bsonAppendStartObject(child_doc, key, &expr);
	bsonAppendStartArray(&expr, "$and", &and_op);
	mongo_append_plain_op(node, &and_op, "0", context);
	mongo_append_value_checks((Node *) node, &and_op, 1, aggIndex, context);
	bsonAppendFinishArray(&expr, &and_op);
	bsonAppendFinishObject(child_doc, &expr);
}

/*
 * mongo_is_foreign_pathkey
 *		Returns true if it's safe to push down the sort expression described by
 *		'pathkey' to the foreign server.
 */
bool
mongo_is_foreign_pathkey(PlannerInfo *root, RelOptInfo *baserel,
						 PathKey *pathkey)
{
	EquivalenceMember *em;
	EquivalenceClass *pathkey_ec = pathkey->pk_eclass;
	Expr	   *em_expr;

	/*
	 * mongo_is_foreign_expr would detect volatile expressions as well, but
	 * checking ec_has_volatile here saves some cycles.
	 */
	if (pathkey_ec->ec_has_volatile)
		return false;

	/* can push if a suitable EC member exists */
	if (!(em = mongo_find_em_for_rel(root, pathkey_ec, baserel)))
		return false;

	/* Ignore binary-compatible relabeling */
	em_expr = em->em_expr;
	while (em_expr && IsA(em_expr, RelabelType))
		em_expr = ((RelabelType *) em_expr)->arg;

	/* Only Vars are allowed per MongoDB. */
	if (!IsA(em_expr, Var))
		return false;

	/* Check for sort operator pushability. */
	if (!mongo_is_default_sort_operator(root, em, pathkey))
		return false;

	return true;
}

/*
 * mongo_is_builtin
 *		Return true if given object is one of PostgreSQL's built-in objects.
 *
 * We use FirstBootstrapObjectId as the cutoff, so that we only consider
 * objects with hand-assigned OIDs to be "built in", not for instance any
 * function or type defined in the information_schema.
 *
 * Our constraints for dealing with types are tighter than they are for
 * functions or operators: we want to accept only types that are in pg_catalog,
 * else format_type might incorrectly fail to schema-qualify their names.
 * (This could be fixed with some changes to format_type, but for now there's
 * no need.)  Thus we must exclude information_schema types.
 *
 * XXX there is a problem with this, which is that the set of built-in
 * objects expands over time.  Something that is built-in to us might not
 * be known to the remote server, if it's of an older version.  But keeping
 * track of that would be a huge exercise.
 */
bool
mongo_is_builtin(Oid oid)
{
	return (oid < FirstGenbkiObjectId);
}
