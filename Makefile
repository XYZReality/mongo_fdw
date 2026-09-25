# mongo_fdw/Makefile
#
# Portions Copyright (c) 2004-2026, EnterpriseDB Corporation.
# Portions Copyright © 2012–2014 Citus Data, Inc.
#

MODULE_big = mongo_fdw

#
# We assume we are running on a POSIX compliant system (Linux, OSX). If you are
# on another platform, change env_posix.os in MONGO_OBJS with the appropriate
# environment object file.
#
LIBJSON = json-c
LIBJSON_OBJS =  $(LIBJSON)/json_util.o $(LIBJSON)/json_object.o $(LIBJSON)/json_tokener.o \
                                $(LIBJSON)/json_object_iterator.o $(LIBJSON)/printbuf.o $(LIBJSON)/linkhash.o \
                                $(LIBJSON)/arraylist.o $(LIBJSON)/random_seed.o $(LIBJSON)/debug.o $(LIBJSON)/strerror_override.o

MONGO_INCLUDE = $(shell pkg-config --cflags libmongoc-1.0)
PG_CPPFLAGS = $(MONGO_INCLUDE) -I$(LIBJSON)
SHLIB_LINK = $(shell pkg-config --libs libmongoc-1.0)

OBJS = connection.o option.o mongo_wrapper.o mongo_fdw.o mongo_query.o deparse.o $(LIBJSON_OBJS)


EXTENSION = mongo_fdw
DATA = mongo_fdw--1.0.sql  mongo_fdw--1.1.sql mongo_fdw--1.0--1.1.sql

REGRESS = server_options connection_validation dml select pushdown join_pushdown aggregate_pushdown limit_offset_pushdown pushdown_semantics
REGRESS_OPTS = --load-extension=$(EXTENSION)

ifdef USE_PGXS
#
# Users need to specify their Postgres installation path through pg_config. For
# example: /usr/local/pgsql/bin/pg_config or /usr/lib/postgresql/9.1/bin/pg_config
#

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

#
# PostgreSQL 19 and later require a C11 compiler: their c.h relies on the
# C11 static_assert(), which is not available under C99.  Earlier releases
# continue to be built with C99.  The major version is determined here, via
# pg_config, because PGXS folds PG_CPPFLAGS into CPPFLAGS at include time, so
# selecting the standard afterwards (using MAJORVERSION) would be too late.
#
PG_MAJORVERSION := $(shell $(PG_CONFIG) --version | sed 's/^[^0-9]*\([0-9][0-9]*\).*/\1/')
ifeq ($(shell test $(PG_MAJORVERSION) -ge 19 && echo yes),yes)
    PG_CPPFLAGS += -std=c11
else
    PG_CPPFLAGS += -std=c99
endif

include $(PGXS)

ifndef MAJORVERSION
    MAJORVERSION := $(basename $(VERSION))
endif

ifeq (,$(findstring $(MAJORVERSION), 14 15 16 17 18 19))
    $(error PostgreSQL 14, 15, 16, 17, 18, or 19 is required to compile this extension)
endif

else
subdir = contrib/mongo_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
