//
// Created by 何文婷 on 18/3/21.
//

#ifndef RASTERDB_RASTERDB_CONFIG_H
#define RASTERDB_RASTERDB_CONFIG_H

#include "rt_context.h"

#include <string.h>


#define CSEQUAL(a,b) (strcmp(a,b)==0)
#define LOCATION_MAXSIZE 512
// Each time fetch how many lines from raster file
#define DEFAULT_BATCHSIZE 100

typedef struct raster_loader_config {
    int rt_file_count;
    char **rt_file;
    char *location;
    int tile_size[2];
    /* SRID of input raster */
    int srid;
    /* SRID of output raster (reprojection) */
    int out_srid;
    char *table;
    char *raster_column;
    int nband_count;
    int pad_tile;
    char *schema;
    char *file_column_name;
    int *nband;
    int hasnodata;
    double nodataval;
    int batchsize;
}RTLOADERCFG;

void rtdealloc_config(RTLOADERCFG *config);
void init_config(RTLOADERCFG *config);
void set_config(RTLOADERCFG **config, char *conf_file);

#endif //RASTERDB_RASTERDB_CONFIG_H
