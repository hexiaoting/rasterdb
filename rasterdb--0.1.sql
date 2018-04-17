-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION rasterdb" to load this file. \quit

CREATE OR REPLACE FUNCTION load_raster(path text)
    RETURNS void 
    AS '$libdir/rasterdb','load_raster'
    LANGUAGE 'c' IMMUTABLE STRICT ;
CREATE OR REPLACE FUNCTION test(cstring)
        RETURNS void 
        AS '$libdir/rasterdb', 'test'
        LANGUAGE 'c' IMMUTABLE STRICT;

CREATE FUNCTION rasterdb_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION rasterdb_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER rasterdb_fdw
  HANDLER rasterdb_fdw_handler
  VALIDATOR rasterdb_fdw_validator;
