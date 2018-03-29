#EXTENSION = rasterdb # the extensions name

#postgres build stuff
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

#CFLAGS=-I /home/hewenting/casearth/gdal-2.2.3/build/include -L/home/hewenting/casearth/gdal-2.2.3/build/lib
CFLAGS="-fPIC"

OBJS=load-raster.o rasterdb_config.o
all: $(OBJS)
clean:
	rm -rf *o
