-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION rasterdb" to load this file. \quit

CREATE OR REPLACE FUNCTION load_raster(path text)
    RETURNS int 
    AS '$libdir/rasterdb','load_raster'
    LANGUAGE 'c' IMMUTABLE STRICT ;
