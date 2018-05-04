create extension postgis;
drop foreign table raster_test;
drop server rasterServer;
drop extension rasterdb;
create extension rasterdb;  
CREATE SERVER rasterServer FOREIGN DATA WRAPPER rasterdb_fdw;
create foreign table raster_test (rast raster) server rasterServer options(conf_file '/home/hewenting/casearth/rasterdb/etc/rasterdb.conf');
select count(*) from raster_test;
