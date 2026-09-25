/*-------------------------------------------------------------------------
 *
 * mongo_wrapper.c
 * 		Wrapper functions for remote MongoDB servers
 *
 * Portions Copyright (c) 2012-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 2004-2026, EnterpriseDB Corporation.
 * Portions Copyright (c) 2012–2014 Citus Data, Inc.
 *
 * IDENTIFICATION
 * 		mongo_wrapper.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>
#include <mongoc.h>
#include "mongo_wrapper.h"

#define ITER_TYPE(i) ((bson_type_t) * ((i)->raw + (i)->type))

/*
 * mongo_uri_escape
 *		Percent-encode a string for use as a component of a MongoDB URI.
 *
 * Only RFC 3986 unreserved characters are passed through, so that user names,
 * passwords, and option values containing '@', ':', '/', '%', '&', etc. are
 * neither misparsed nor able to inject extra URI options.
 */
static char *
mongo_uri_escape(const char *str)
{
	StringInfoData buf;
	const unsigned char *p;

	initStringInfo(&buf);
	for (p = (const unsigned char *) str; *p; p++)
	{
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
			(*p >= '0' && *p <= '9') ||
			*p == '-' || *p == '.' || *p == '_' || *p == '~')
			appendStringInfoChar(&buf, (char) *p);
		else
			appendStringInfo(&buf, "%%%02X", *p);
	}

	return buf.data;
}

/*
 * mongoConnect
 *		Connect to MongoDB server using Host/ip and Port number.
 */
MONGO_CONN *
mongoConnect(MongoFdwOptions *opt)
{
	MONGO_CONN *client;
	mongoc_uri_t *uri;
	bson_error_t error;
	StringInfoData uristr;

	initStringInfo(&uristr);
	appendStringInfoString(&uristr, "mongodb://");
	if (opt->svr_username && opt->svr_password)
		appendStringInfo(&uristr, "%s:%s@",
						 mongo_uri_escape(opt->svr_username),
						 mongo_uri_escape(opt->svr_password));
	appendStringInfo(&uristr, "%s:%hu/%s?ssl=%s",
					 opt->svr_address, opt->svr_port,
					 mongo_uri_escape(opt->svr_database),
					 opt->ssl ? "true" : "false");
	if (opt->readPreference)
		appendStringInfo(&uristr, "&readPreference=%s",
						 mongo_uri_escape(opt->readPreference));
	if (opt->authenticationDatabase)
		appendStringInfo(&uristr, "&authSource=%s",
						 mongo_uri_escape(opt->authenticationDatabase));
	if (opt->replicaSet)
		appendStringInfo(&uristr, "&replicaSet=%s",
						 mongo_uri_escape(opt->replicaSet));

	uri = mongoc_uri_new_with_error(uristr.data, &error);

	/* The URI may contain the password, so don't leave it lying around */
	memset(uristr.data, 0, uristr.len);
	pfree(uristr.data);

	if (uri == NULL)
		ereport(ERROR,
				(errmsg("could not connect to %s:%d", opt->svr_address,
						opt->svr_port),
				 errhint("Mongo error: \"%s\"", error.message)));

	client = mongoc_client_new_from_uri(uri);
	mongoc_uri_destroy(uri);

	if (client == NULL)
		ereport(ERROR,
				(errmsg("could not connect to %s:%d", opt->svr_address,
						opt->svr_port),
				 errhint("Mongo driver connection error.")));

	if (opt->ssl)
	{
		/* Start from the driver defaults so that no field is left unset */
		mongoc_ssl_opt_t ssl_opts = *mongoc_ssl_opt_get_default();

		ssl_opts.pem_file = opt->pem_file;
		ssl_opts.pem_pwd = opt->pem_pwd;
		ssl_opts.ca_file = opt->ca_file;
		ssl_opts.ca_dir = opt->ca_dir;
		ssl_opts.crl_file = opt->crl_file;
		ssl_opts.weak_cert_validation = opt->weak_cert_validation;
		mongoc_client_set_ssl_opts(client, &ssl_opts);
	}

	return client;
}

/*
 * mongoDisconnect
 *		Disconnect from MongoDB server.
 */
void
mongoDisconnect(MONGO_CONN *conn)
{
	if (conn)
		mongoc_client_destroy(conn);
}

/*
 * mongoInsert
 *		Insert a document 'b' into MongoDB.
 */
bool
mongoInsert(MONGO_CONN *conn, char *database, char *collection, BSON *b)
{
	mongoc_collection_t *c;
	bson_error_t error;
	bool		r = false;

	c = mongoc_client_get_collection(conn, database, collection);

	r = mongoc_collection_insert(c, MONGOC_INSERT_NONE, b, NULL, &error);
	mongoc_collection_destroy(c);
	if (!r)
		ereport(ERROR,
				(errmsg("failed to insert row"),
				 errhint("Mongo error: \"%s\"", error.message)));

	return true;
}

/*
 * reply_count
 *		Extract an integer count field (e.g. "matchedCount") from a write
 *		command reply.
 */
static int64
reply_count(const BSON *reply, const char *field)
{
	bson_iter_t it;

	if (bson_iter_init_find(&it, reply, field) && BSON_ITER_HOLDS_NUMBER(&it))
		return bson_iter_as_int64(&it);

	return 0;
}

/*
 * mongoUpdate
 *		Update the single document matching filter 'b' using update
 *		document 'op'.
 *
 * Returns true if a document matched the filter.
 */
bool
mongoUpdate(MONGO_CONN *conn, char *database, char *collection, BSON *b,
			BSON *op)
{
	mongoc_collection_t *c;
	bson_error_t error;
	bson_t		reply;
	bool		r;
	int64		matched;

	c = mongoc_client_get_collection(conn, database, collection);

	r = mongoc_collection_update_one(c, b, op, NULL, &reply, &error);
	mongoc_collection_destroy(c);
	matched = reply_count(&reply, "matchedCount");
	bson_destroy(&reply);

	if (!r)
		ereport(ERROR,
				(errmsg("failed to update row"),
				 errhint("Mongo error: \"%s\"", error.message)));

	return matched > 0;
}

/*
 * mongoDelete
 *		Delete the single document matching filter 'b'.
 *
 * Returns true if a document was deleted.
 */
bool
mongoDelete(MONGO_CONN *conn, char *database, char *collection, BSON *b)
{
	mongoc_collection_t *c;
	bson_error_t error;
	bson_t		reply;
	bool		r;
	int64		deleted;

	c = mongoc_client_get_collection(conn, database, collection);

	r = mongoc_collection_delete_one(c, b, NULL, &reply, &error);
	mongoc_collection_destroy(c);
	deleted = reply_count(&reply, "deletedCount");
	bson_destroy(&reply);

	if (!r)
		ereport(ERROR,
				(errmsg("failed to delete row"),
				 errhint("Mongo error: \"%s\"", error.message)));

	return deleted > 0;
}

/*
 * mongoCursorCreate
 *		Performs a query against the configured MongoDB server and return
 *		cursor which can be destroyed by calling mongoCursorDestroy.
 */
MONGO_CURSOR *
mongoCursorCreate(MONGO_CONN *conn, char *database, char *collection, BSON *q)
{
	mongoc_collection_t *c;
	MONGO_CURSOR *cur;
	bson_error_t error;

	c = mongoc_client_get_collection(conn, database, collection);
	cur = mongoc_collection_aggregate(c, MONGOC_QUERY_NONE, q, NULL, NULL);
	mongoc_collection_destroy(c);

	if (mongoc_cursor_error(cur, &error))
	{
		mongoc_cursor_destroy(cur);
		ereport(ERROR,
				(errmsg("failed to create cursor"),
				 errhint("Mongo error: \"%s\"", error.message)));
	}

	return cur;
}

/*
 * mongoCursorDestroy
 *		Destroy cursor created by calling mongoCursorCreate function.
 */
void
mongoCursorDestroy(MONGO_CURSOR *c)
{
	mongoc_cursor_destroy(c);
}


/*
 * mongoCursorBson
 *		Get the current document from cursor.
 */
const BSON *
mongoCursorBson(MONGO_CURSOR *c)
{
	return mongoc_cursor_current(c);
}

/*
 * mongoCursorNext
 *		Advance the cursor to the next document, which can then be fetched
 *		using mongoCursorBson().
 *
 * Returns false once the cursor is exhausted.  Any error raised by the server
 * or the driver while iterating is reported rather than being mistaken for the
 * end of the result set.
 */
bool
mongoCursorNext(MONGO_CURSOR *c)
{
	const BSON *doc;
	bson_error_t error;

	if (mongoc_cursor_next(c, &doc))
		return true;

	if (mongoc_cursor_error(c, &error))
		ereport(ERROR,
				(errmsg("could not iterate over mongo collection"),
				 errhint("Mongo error: \"%s\"", error.message)));

	return false;
}

/*
 * bsonCreate
 *		Allocates and initializes a new bson_t structure, which must be freed
 *		with bsonDestroy().
 */
BSON *
bsonCreate(void)
{
	return bson_new();
}

/*
 * bsonDestroy
 *		Destroy Bson object created by bsonCreate function.
 */
void
bsonDestroy(BSON *b)
{
	bson_destroy(b);
}

/*
 * bsonIterInit
 *		Initialize the bson Iterator.
 */
bool
bsonIterInit(BSON_ITERATOR *it, BSON *b)
{
	return bson_iter_init(it, b);
}

bool
bsonIterSubObject(BSON_ITERATOR *it, BSON *b)
{
	const uint8_t *buffer;
	uint32_t	len;

	bson_iter_document(it, &len, &buffer);
	bson_init_static(b, buffer, len);

	return true;
}

/*
 * bson_double_to_int
 *		Validate a BSON double against the bounds of the target integer
 *		type, rejecting NaN, Infinity, and out-of-range values, and return
 *		it converted to int64.
 *
 * maxExact should be true when maxVal is exactly representable as a double
 * (int16/int32 bounds are), and false when it isn't (the int64 max isn't,
 * since it rounds up to the next representable double), so that values at
 * or above the rounded max are rejected too.
 */
static int64
bson_double_to_int(double val, int64 minVal, int64 maxVal, bool maxExact,
					const char *typeName)
{
	/* Check for NaN */
	if (isnan(val))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("cannot convert NaN to %s", typeName)));

	/* Check for Infinity */
	if (isinf(val))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("cannot convert infinity to %s", typeName)));

	/*
	 * Round to the nearest integer first, matching PostgreSQL's own
	 * float-to-integer coercion (see dtoi2/dtoi4/dtoi8 in float.c), rather
	 * than truncating toward zero.  This must happen before the range
	 * check below, since rounding can itself push a value out of range
	 * (e.g. a value just under INT32_MAX that rounds up past it).
	 */
	val = rint(val);

	/* Check for integer range overflow */
	if (val < (double) minVal ||
		(maxExact ? val > (double) maxVal : val >= (double) maxVal))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value \"%f\" is out of range for type %s",
						val, typeName)));

	return (int64) val;
}

/*
 * bson_int64_to_int
 *		Validate a BSON int64 against the bounds of the target integer
 *		type, rejecting out-of-range values, and return it.
 */
static int64
bson_int64_to_int(int64 val, int64 minVal, int64 maxVal, const char *typeName)
{
	if (val < minVal || val > maxVal)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value \"" INT64_FORMAT "\" is out of range for type %s",
						val, typeName)));

	return val;
}

int32_t
bsonIterInt32(BSON_ITERATOR *it)
{
	BSON_ASSERT(it);
	switch ((int) ITER_TYPE(it))
	{
		case BSON_TYPE_BOOL:
			return (int32) bson_iter_bool(it);
		case BSON_TYPE_DOUBLE:
			return (int32) bson_double_to_int(bson_iter_double(it),
											  PG_INT32_MIN, PG_INT32_MAX,
											  true, "integer");
		case BSON_TYPE_INT64:
			return (int32) bson_int64_to_int(bson_iter_int64(it),
											 PG_INT32_MIN, PG_INT32_MAX,
											 "integer");
		case BSON_TYPE_INT32:
			return bson_iter_int32(it);
		default:
			return 0;
	}
}

int64_t
bsonIterInt64(BSON_ITERATOR *it)
{
	BSON_ASSERT(it);

	/*
	 * Handle Double separately to prevent undefined behavior from NaN,
	 * Infinity, or overflow during the cast to int64.
	 */
	if (ITER_TYPE(it) == BSON_TYPE_DOUBLE)
		return bson_double_to_int(bson_iter_double(it),
								  PG_INT64_MIN, PG_INT64_MAX,
								  false, "bigint");

	/*
	 * For all other types (Int32, Int64, Bool), the driver's standard
	 * convenience function is safe to use.
	 */
	return bson_iter_as_int64(it);
}

int16_t
bsonIterInt16(BSON_ITERATOR *it)
{
	BSON_ASSERT(it);

	/*
	 * Handle Double and Int64 separately so NaN/Infinity/out-of-range
	 * values raise a smallint-specific error.  In particular, Int64 can't
	 * be routed through bsonIterInt32(), since a value outside int32's
	 * range would be rejected there with an "integer" message even though
	 * the target type is smallint.
	 */
	switch ((int) ITER_TYPE(it))
	{
		case BSON_TYPE_DOUBLE:
			return (int16) bson_double_to_int(bson_iter_double(it),
											  PG_INT16_MIN, PG_INT16_MAX,
											  true, "smallint");
		case BSON_TYPE_INT64:
			return (int16) bson_int64_to_int(bson_iter_int64(it),
											 PG_INT16_MIN, PG_INT16_MAX,
											 "smallint");
		default:
			/* BSON_TYPE_BOOL, BSON_TYPE_INT32: bsonIterInt32() can't error
			 * for these, so the range check below always reports the
			 * correct type name. */
			{
				int32		val = bsonIterInt32(it);

				if (val < PG_INT16_MIN || val > PG_INT16_MAX)
					ereport(ERROR,
							(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
							 errmsg("value \"%d\" is out of range for type smallint",
									val)));

				return (int16) val;
			}
	}
}

double
bsonIterDouble(BSON_ITERATOR *it)
{
	return bson_iter_as_double(it);
}

bool
bsonIterBool(BSON_ITERATOR *it)
{
	return bson_iter_as_bool(it);
}

const char *
bsonIterString(BSON_ITERATOR *it)
{
	uint32_t	len = 0;

	return bson_iter_utf8(it, &len);
}

const char *
bsonIterBinData(BSON_ITERATOR *it, uint32_t *len)
{
	const uint8_t *binary = NULL;
	bson_subtype_t subtype = BSON_SUBTYPE_BINARY;

	bson_iter_binary(it, &subtype, len, &binary);

	return (char *) binary;
}

const bson_oid_t *
bsonIterOid(BSON_ITERATOR *it)
{
	return bson_iter_oid(it);
}

time_t
bsonIterDate(BSON_ITERATOR *it)
{
	return bson_iter_date_time(it);
}

const char *
bsonIterKey(BSON_ITERATOR *it)
{
	return bson_iter_key(it);
}

int
bsonIterType(BSON_ITERATOR *it)
{
	return bson_iter_type(it);
}

int
bsonIterNext(BSON_ITERATOR *it)
{
	return bson_iter_next(it);
}

bool
bsonIterSubIter(BSON_ITERATOR *it, BSON_ITERATOR *sub)
{
	return bson_iter_recurse(it, sub);
}

void
bsonOidFromString(bson_oid_t *o, char *str)
{
	bson_oid_init_from_string(o, str);
}

bool
bsonAppendOid(BSON *b, const char *key, bson_oid_t *v)
{
	return bson_append_oid(b, key, strlen(key), v);
}

bool
bsonAppendBool(BSON *b, const char *key, bool v)
{
	return bson_append_bool(b, key, -1, v);
}

bool
bsonAppendStartObject(BSON *b, char *key, BSON *r)
{
	return bson_append_document_begin(b, key, strlen(key), r);
}

bool
bsonAppendFinishObject(BSON *b, BSON *r)
{
	return bson_append_document_end(b, r);
}

bool
bsonAppendNull(BSON *b, const char *key)
{
	return bson_append_null(b, key, strlen(key));
}

bool
bsonAppendInt32(BSON *b, const char *key, int v)
{
	return bson_append_int32(b, key, strlen(key), v);
}

bool
bsonAppendInt64(BSON *b, const char *key, int64_t v)
{
	return bson_append_int64(b, key, strlen(key), v);
}

bool
bsonAppendDouble(BSON *b, const char *key, double v)
{
	return bson_append_double(b, key, strlen(key), v);
}

bool
bsonAppendUTF8(BSON *b, const char *key, char *v)
{

	return bson_append_utf8(b, key, strlen(key), v, strlen(v));
}

bool
bsonAppendBinary(BSON *b, const char *key, char *v, size_t len)
{
	return bson_append_binary(b, key, (int) strlen(key), BSON_SUBTYPE_BINARY,
							  (const uint8_t *) v, len);
}

bool
bsonAppendDate(BSON *b, const char *key, time_t v)
{
	return bson_append_date_time(b, key, strlen(key), v);
}

bool
bsonAppendBson(BSON *b, char *key, BSON *c)
{
	return bson_append_document(b, key, strlen(key), c);
}

bool
bsonAppendStartArray(BSON *b, const char *key, BSON *c)
{
	return bson_append_array_begin(b, key, -1, c);
}

bool
bsonAppendFinishArray(BSON *b, BSON *c)
{
	return bson_append_array_end(b, c);
}

bool
jsonToBsonAppendElement(BSON *bb, const char *k, struct json_object *v)
{
	bool		status = true;

	if (!v)
	{
		bsonAppendNull(bb, k);
		return status;
	}

	switch (json_object_get_type(v))
	{
		case json_type_int:
			{
				int64		ival = json_object_get_int64(v);

				/* Keep small integers as int32, like mongoimport does */
				if (ival >= PG_INT32_MIN && ival <= PG_INT32_MAX)
					bsonAppendInt32(bb, k, (int32) ival);
				else
					bsonAppendInt64(bb, k, ival);
			}
			break;
		case json_type_boolean:
			bsonAppendBool(bb, k, json_object_get_boolean(v));
			break;
		case json_type_double:
			bsonAppendDouble(bb, k, json_object_get_double(v));
			break;
		case json_type_string:
			bsonAppendUTF8(bb, k, (char *) json_object_get_string(v));
			break;
		case json_type_object:
			{
				BSON		t;
				struct json_object *joj;

				joj = json_object_object_get(v, "$oid");

				if (joj != NULL)
				{
					bson_oid_t	bsonObjectId;

					memset(bsonObjectId.bytes, 0, sizeof(bsonObjectId.bytes));
					bsonOidFromString(&bsonObjectId, (char *) json_object_get_string(joj));
					status = bsonAppendOid(bb, k, &bsonObjectId);
					break;
				}
				joj = json_object_object_get(v, "$date");
				if (joj != NULL)
				{
					status = bsonAppendDate(bb, k, json_object_get_int64(joj));
					break;
				}
				bsonAppendStartObject(bb, (char *) k, &t);

				{
					json_object_object_foreach(v, kk, vv)
						jsonToBsonAppendElement(&t, kk, vv);
				}
				bsonAppendFinishObject(bb, &t);
			}
			break;
		case json_type_array:
			{
				int			i;
				char		buf[12];
				BSON		t;

				bsonAppendStartArray(bb, k, &t);
				for (i = 0; i < json_object_array_length(v); i++)
				{
					snprintf(buf, sizeof(buf), "%d", i);
					jsonToBsonAppendElement(&t, buf, json_object_array_get_idx(v, i));
				}
				bsonAppendFinishArray(bb, &t);
			}
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_FDW_INVALID_DATA_TYPE),
					 errmsg("can't handle type for : %s",
							json_object_to_json_string(v))));
	}

	return status;
}

json_object *
jsonTokenerPrase(char *s)
{
	return json_tokener_parse(s);
}

/*
 * mongoAggregateCount
 *		Count the number of documents.
 */
double
mongoAggregateCount(MONGO_CONN *conn, const char *database,
					const char *collection, const BSON *b)
{
	BSON	   *command;
	BSON	   *reply;
	double		count = 0;
	bson_error_t error;
	bool		retval;
	bson_iter_t it;

	command = bsonCreate();
	reply = bsonCreate();
	bsonAppendUTF8(command, "count", (char *) collection);
	if (b)						/* Not empty */
		bsonAppendBson(command, "query", (BSON *) b);

	retval = mongoc_client_command_simple(conn, database, command, NULL, reply,
										  &error);
	if (!retval)
		ereport(ERROR,
				(errmsg("failed to get the document count"),
				 errhint("Mongo error: \"%s\"", error.message)));

	if (bson_iter_init_find(&it, reply, "n"))
		count = bsonIterDouble(&it);

	bsonDestroy(reply);
	bsonDestroy(command);

	return count;
}

void
bsonOidToString(const bson_oid_t *o, char str[25])
{
	bson_oid_to_string(o, str);
}

const char *
bsonIterCode(BSON_ITERATOR *i)
{
	return bson_iter_code(i, NULL);
}

const char *
bsonIterRegex(BSON_ITERATOR *i)
{
	return bson_iter_regex(i, NULL);
}

const bson_value_t *
bsonIterValue(BSON_ITERATOR *i)
{
	return bson_iter_value(i);
}

void
bsonToJsonStringValue(StringInfo output, BSON_ITERATOR *iter, bool isArray)
{
	if (isArray)
		dumpJsonArray(output, iter);
	else
		dumpJsonObject(output, iter);
}

/*
 * dumpJsonObject
 *		Converts BSON document to a JSON string.
 *
 * isArray signifies if bsonData is contents of array or object.
 * [Some of] special BSON datatypes are converted to JSON using
 * "Strict MongoDB Extended JSON" [1].
 *
 * [1] http://docs.mongodb.org/manual/reference/mongodb-extended-json/
 */
void
dumpJsonObject(StringInfo output, BSON_ITERATOR *iter)
{
	uint32_t	len;
	const uint8_t *data;
	BSON 		bson;

	bson_iter_document(iter, &len, &data);
	if (bson_init_static(&bson, data, len))
	{
		char	   *json = bsonAsJson(&bson);

		if (json != NULL)
		{
			appendStringInfoString(output, json);
			bson_free(json);
		}
	}
}

void
dumpJsonArray(StringInfo output, BSON_ITERATOR *iter)
{
	uint32_t	len;
	const uint8_t *data;
	BSON 		bson;

	bson_iter_array(iter, &len, &data);
	if (bson_init_static(&bson, data, len))
	{
		char	   *json;

		if ((json = bson_array_as_legacy_extended_json(&bson, NULL)))
		{
			appendStringInfoString(output, json);
			bson_free(json);
		}
	}
}

char *
bsonAsJson(const BSON *bsonDocument)
{
	return bson_as_legacy_extended_json(bsonDocument, NULL);
}
