//
// Created by 何文婷 on 18/3/21.
//

#ifndef RASTERDB_LOAD_RASTER_H
#define RASTERDB_LOAD_RASTER_H

#include <stdlib.h>
#include "rasterdb_config.h"
#include "rt_context.h"
#include "librtcore.h"
#include "gdal.h"

typedef struct rasterinfo_t {
    /* SRID of raster */
    int srid;
    /* srs of raster */
    char *srs;
    /* width, height */
    uint32_t dim[2];
    /* number of bands */
    int *nband; /* 1-based */
    int nband_count;
    /* array of pixeltypes */
    GDALDataType *gdalbandtype;
    rt_pixtype *bandtype;
    /* array of hasnodata flags */
    int *hasnodata;
    /* array of nodatavals */
    double *nodataval;
    /* geotransform matrix */
    double gt[6];
    /* tile size */
    int tile_size[2];
} RASTERINFO;

int analysis_raster(char *filename, RTLOADERCFG *config, int cur_lineno, char **buf);
int convert_raster(char *filename, RTLOADERCFG *config, RASTERINFO *info, int cur_lineno, char **buf);

#endif //RASTERDB_LOAD_RASTER_H
