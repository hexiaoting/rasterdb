EXTENSION = rasterdb # the extensions name
DATA = sql/rasterdb--0.1.sql  # script files to install

#postgres build stuff
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
