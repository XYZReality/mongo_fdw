#!/bin/sh
# Defaults only when unset, so the same script works against a MongoDB with
# authentication disabled -- which the regression suites require, since every
# one of them creates a credential-less user mapping. Set MONGO_USER_NAME empty
# to omit the authentication flags entirely.
MONGO_HOST="${MONGO_HOST:-localhost}"
MONGO_PORT="${MONGO_PORT:-27017}"
MONGO_USER_NAME="${MONGO_USER_NAME-edb}"
MONGO_PWD="${MONGO_PWD-edb}"
export MONGO_HOST MONGO_PORT MONGO_USER_NAME MONGO_PWD

if [ -n "$MONGO_USER_NAME" ]; then
    AUTH="-u $MONGO_USER_NAME -p $MONGO_PWD"
    SHELL_AUTH="$AUTH --authenticationDatabase mongo_fdw_regress"
else
    AUTH=""
    SHELL_AUTH=""
fi

# Below commands must be run in MongoDB to create mongo_fdw_regress and mongo_fdw_regress1 databases
# used in regression tests with edb user and edb password.

# use mongo_fdw_regress
# db.createUser({user:"edb",pwd:"edb",roles:[{role:"dbOwner", db:"mongo_fdw_regress"},{role:"readWrite", db:"mongo_fdw_regress"}]})
# use mongo_fdw_regress1
# db.createUser({user:"edb",pwd:"edb",roles:[{role:"dbOwner", db:"mongo_fdw_regress1"},{role:"readWrite", db:"mongo_fdw_regress1"}]})
# use mongo_fdw_regress2
# db.createUser({user:"edb",pwd:"edb",roles:[{role:"dbOwner", db:"mongo_fdw_regress2"},{role:"readWrite", db:"mongo_fdw_regress2"}]})

mongoimport --host=$MONGO_HOST --port=$MONGO_PORT $AUTH --db mongo_fdw_regress --collection countries --jsonArray --drop --maintainInsertionOrder --quiet < data/mongo_fixture.json
mongoimport --host=$MONGO_HOST --port=$MONGO_PORT $AUTH --db mongo_fdw_regress --collection warehouse --jsonArray --drop --maintainInsertionOrder --quiet < data/mongo_warehouse.json
mongoimport --host=$MONGO_HOST --port=$MONGO_PORT $AUTH --db mongo_fdw_regress --collection testlog --jsonArray --drop --maintainInsertionOrder --quiet < data/mongo_testlog.json
mongoimport --host=$MONGO_HOST --port=$MONGO_PORT $AUTH --db mongo_fdw_regress --collection testdevice --jsonArray --drop --maintainInsertionOrder --quiet < data/mongo_testdevice.json
mongosh --host=$MONGO_HOST --port=$MONGO_PORT $SHELL_AUTH < data/mongo_test_data.js > /dev/null
