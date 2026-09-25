\set MONGO_HOST			`echo \'"$MONGO_HOST"\'`
\set MONGO_PORT			`echo \'"$MONGO_PORT"\'`
\set MONGO_USER_NAME	`echo \'"$MONGO_USER_NAME"\'`
\set MONGO_PASS			`echo \'"$MONGO_PWD"\'`

-- Before running this file user must create database mongo_fdw_regress on
-- MongoDB with all permission for MONGO_USER_NAME user with MONGO_PASS
-- password and ran mongodb_init.sh file to load collections.

-- Pushed-down operations must give the same results as evaluating them
-- locally, even where MongoDB's semantics differ from PostgreSQL's.

\c contrib_regression
CREATE EXTENSION IF NOT EXISTS mongo_fdw;
CREATE SERVER mongo_server FOREIGN DATA WRAPPER mongo_fdw
  OPTIONS (address :MONGO_HOST, port :MONGO_PORT);
CREATE USER MAPPING FOR public SERVER mongo_server;

-- The test_semantics collection holds ObjectId and string _ids, and fields
-- that are missing, null, or of a type other than the column's.
CREATE FOREIGN TABLE sem_name (_id NAME, g int, n int, s text, b bool, x int)
  SERVER mongo_server OPTIONS (database 'mongo_fdw_regress', collection 'test_semantics');
CREATE FOREIGN TABLE sem_text (_id text, g int)
  SERVER mongo_server OPTIONS (database 'mongo_fdw_regress', collection 'test_semantics');
CREATE FOREIGN TABLE sem_empty (_id NAME, a int)
  SERVER mongo_server OPTIONS (database 'mongo_fdw_regress', collection 'test_semantics_empty');

--
-- ObjectIds
--
-- ObjectIds and strings are both presented as strings.
SELECT _id, g FROM sem_name ORDER BY g, _id COLLATE "C";
SELECT _id, g FROM sem_text ORDER BY g, _id COLLATE "C";
-- A value that is an ObjectId string matches the ObjectId, whatever the type
-- of the value (name, text, or a text parameter).
EXPLAIN (VERBOSE, COSTS OFF)
SELECT g FROM sem_name WHERE _id = '62b597048a7fca1c83fc4eea';
SELECT g FROM sem_name WHERE _id = '62b597048a7fca1c83fc4eea';
SELECT g FROM sem_name WHERE _id = '62b597048a7fca1c83fc4eea'::text;
SELECT g FROM sem_text WHERE _id = '62b597048a7fca1c83fc4eea';
PREPARE by_id(text) AS SELECT g FROM sem_name WHERE _id = $1;
EXECUTE by_id('62b597048a7fca1c83fc4eeb');
EXECUTE by_id('plain-string-id');
EXECUTE by_id(NULL);
DEALLOCATE by_id;
-- Strings still match strings, including ones starting with "$".
SELECT g FROM sem_text WHERE _id = 'plain-string-id';
SELECT g FROM sem_text WHERE _id = '$dollar';
SELECT g FROM sem_name WHERE s = '$s';
SELECT count(*) FROM sem_name WHERE _id <> '62b597048a7fca1c83fc4eea';
-- The pushed-down query compares with both the string and the ObjectId.
SET mongo_fdw.log_remote_query TO true;
SET client_min_messages TO log;
SELECT g FROM sem_text WHERE _id = '62b597048a7fca1c83fc4eea';
RESET client_min_messages;
RESET mongo_fdw.log_remote_query;
-- UPDATE and DELETE find documents by either kind of _id.
UPDATE sem_name SET g = g WHERE _id = '62b597048a7fca1c83fc4eec';
UPDATE sem_name SET g = g WHERE _id = 'plain-string-id';
UPDATE sem_text SET g = g WHERE _id = '62b597048a7fca1c83fc4eea';

--
-- Missing, null, and mistyped fields don't match, as they are NULL here.
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT n FROM sem_name WHERE n < 5 ORDER BY n;
SELECT n FROM sem_name WHERE n < 5 ORDER BY n;
SELECT count(*) FROM sem_name WHERE n > 1;
SELECT count(*) FROM sem_name WHERE n <> 3;
SELECT g FROM sem_name WHERE x < 1;
SELECT b FROM sem_name WHERE b;
SELECT b FROM sem_name WHERE NOT b;
SELECT count(*) FROM sem_name WHERE NOT (n > 1 OR g = 3);

--
-- Operators that MongoDB evaluates differently are evaluated locally.
--
-- Integer division truncates in PostgreSQL but not in MongoDB.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT n FROM sem_name WHERE n / 2 = 1;
SELECT n FROM sem_name WHERE n / 2 = 1;
-- MongoDB orders strings bytewise, which only matches the "C" collation.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT s FROM sem_name ORDER BY s COLLATE "en_US" NULLS FIRST;
SELECT s FROM sem_name ORDER BY s COLLATE "en_US" NULLS FIRST;
EXPLAIN (VERBOSE, COSTS OFF)
SELECT s FROM sem_name ORDER BY s COLLATE "C" NULLS FIRST;
SELECT s FROM sem_name ORDER BY s COLLATE "C" NULLS FIRST;
EXPLAIN (VERBOSE, COSTS OFF)
SELECT s FROM sem_name WHERE s COLLATE "C" < 'a' ORDER BY 1;
SELECT s FROM sem_name WHERE s COLLATE "C" < 'a' ORDER BY 1;
-- Errors raised by MongoDB are reported, rather than ending the scan early.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT n FROM sem_name WHERE n % 0 = 1;
SELECT n FROM sem_name WHERE n % 0 = 1;

--
-- Aggregates
--
-- An aggregate without GROUP BY returns a row even if there is no input.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT count(*) FROM sem_name WHERE g = 99;
SELECT count(*) FROM sem_name WHERE g = 99;
SELECT count(*), sum(a), min(a), avg(a) FROM sem_empty;
SELECT count(*) FROM sem_empty HAVING count(*) = 0;
SELECT count(*) FROM sem_empty LIMIT 1 OFFSET 1;
-- SUM() of no non-null input is NULL; values of other types are ignored.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT g, sum(x), sum(n), max(n) FROM sem_name GROUP BY g ORDER BY g;
SELECT g, sum(x), sum(n), max(n) FROM sem_name GROUP BY g ORDER BY g;
-- Missing and mistyped values group with nulls.
SELECT n, count(*) FROM sem_name GROUP BY n ORDER BY n;
-- Several aggregates in HAVING.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT g FROM sem_name GROUP BY g HAVING min(n) > 2 AND max(n) < 4 ORDER BY g;
SELECT g FROM sem_name GROUP BY g HAVING min(n) > 2 AND max(n) < 4 ORDER BY g;
SELECT g FROM sem_name GROUP BY g HAVING min(n) < 4 ORDER BY g;

--
-- LIMIT
--
SELECT g FROM sem_name ORDER BY g LIMIT 0;
SELECT count(*) FROM (SELECT g FROM sem_name ORDER BY g LIMIT 3000000000) x;

-- Cleanup
DROP FOREIGN TABLE sem_name;
DROP FOREIGN TABLE sem_text;
DROP FOREIGN TABLE sem_empty;
DROP USER MAPPING FOR public SERVER mongo_server;
DROP SERVER mongo_server;
DROP EXTENSION mongo_fdw;
