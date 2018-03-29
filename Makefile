MODULE=rasterdb
MODULE_big=rasterdb
SUBDIRS = loader core extensions
SHLIB_LINK=-lgdal -lrtpostgis
override CFLAGS=
override LDFLAGS = -L$(libdir)

SRC = core/rt_context.c core/rt_util.c  loader/load-raster.c loader/rasterdb_config.c rasterdb.c
OBJS = $(SRC:%.c=%.o)
all: $(OBJS) 

install:
	$(MAKE) install -C extensions

#EXTENSION = rasterdb
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
