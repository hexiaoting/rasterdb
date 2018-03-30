# rasterdb/Makefile
#
# Copyright (c) 2018 Institute of Computing Technology, Chinese Academy of Sciences
#

MODULE_big=rasterdb
SHLIB_LINK= -L/home/gpadmin/dev/lib/postgresql -lgdal -lrtpostgis

OBJS = rt_context.o rt_util.o load_raster.o rasterdb_config.o rasterdb.o

EXTENSION = rasterdb
DATA = rasterdb--0.1.sql


PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
