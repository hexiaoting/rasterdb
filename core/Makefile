#EXTENSION = rasterdb # the extensions name

#postgres build stuff
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

#CFLAGS=-I /home/hewenting/casearth/gdal-2.2.3/build/include -L/home/hewenting/casearth/gdal-2.2.3/build/lib
CFLAGS = -g -O0  -fPIC -DPIC  -Wall -Wmissing-prototypes 

OBJS=rt_context.o rt_util.o
all: $(OBJS)
clean:
	rm -rf *o
