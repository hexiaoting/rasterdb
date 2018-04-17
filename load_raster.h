//
// Created by 何文婷 on 18/3/21.
//

#ifndef RASTERDB_LOAD_RASTER_H
#define RASTERDB_LOAD_RASTER_H

#include "rasterdb_config.h"
#include "rt_context.h"
#include "librtcore.h"

#include "gdal.h"

#include <stdlib.h>


typedef struct stringbuffer_t {
    uint32_t length;
    char **line;
} STRINGBUFFER;

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

//void analysis_raster(RTLOADERCFG *config);
int analysis_raster(RTLOADERCFG *config, int cur_linno, int batchsize, char **buf);

int append_stringbuffer(STRINGBUFFER *buffer, const char *str);
void rtdealloc_stringbuffer(STRINGBUFFER *buffer, int freebuffer);
int
process_rasters(RTLOADERCFG *config, int cur_linno, int batchsize, char **buf);
//process_rasters(RTLOADERCFG *config, STRINGBUFFER *buffer);
int create_table(RTLOADERCFG *config, STRINGBUFFER *buffer);
int convert_raster(int idx, RTLOADERCFG *config, RASTERINFO *info, int cur_linno, int batchsize, char **buf);
//int convert_raster(int idx, RTLOADERCFG *config, RASTERINFO *info, STRINGBUFFER *tileset, STRINGBUFFER *buffer);
int insert_records(char *filename, char *schema, char *table, char *rast_column, char *file_column_name,
                   int out_srid,
                   STRINGBUFFER *tileset, STRINGBUFFER *buffer);

#endif //RASTERDB_LOAD_RASTER_H
