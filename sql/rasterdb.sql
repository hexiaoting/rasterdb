drop foreign table raster_test;
drop server rasterServer;
drop extension rasterdb;
create extension rasterdb;  
CREATE SERVER rasterServer FOREIGN DATA WRAPPER rasterdb_fdw;
create foreign table raster_test (rast raster) server rasterServer options(location '/home/hewenting/casearth/rasterdb/input/output.tiff', tile_size '100x100');
select * from raster_test limit 1
