//
// Created by 何文婷 on 18/3/21.
//

#ifndef RASTERDB_RASTERDB_CONFIG_H
#define RASTERDB_RASTERDB_CONFIG_H

#include "rt_context.h"

#include <string.h>


#define CSEQUAL(a,b) (strcmp(a,b)==0)

typedef struct raster_loader_config {
    int rt_file_count;
    char **rt_file;
    char **rt_filename;
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
}RTLOADERCFG;

void rtdealloc_config(RTLOADERCFG *config);
void init_config(RTLOADERCFG *config);

#endif //RASTERDB_RASTERDB_CONFIG_H
