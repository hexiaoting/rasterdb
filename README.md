# rasterdb
Compile and install:
	make && make install

Usage:
	psql# create extension rasterdb;
	psql# select load_raster("/tmp/data.tiff")
