rasterdb
==============

This extension implements a series of functions related to PostGIS raster data, including
data parsing, loading and analyzing.

S1: Compile and Install:
make clean
make
make install

S2: Modify conf file. Default: etc/rasterdb.conf
    #tile_size=100x100
    #location must be an absolute directory
    #location=/home/hewenting/casearth/rasterdb/input/
    #batchsize=50

S3:Test:
psql -f sql/rasterdb.sql
