//
// Created by 何文婷 on 18/3/22.
//
#include "rasterdb_config.h"

void
init_config(RTLOADERCFG *config) {
    config->rt_file_count = 0;
    config->rt_file = NULL;
    config->rt_filename = NULL;
    config->schema = "public";
    config->table = "test_raster";
    config->raster_column = "rast";
    config->file_column_name = "filename";
    config->srid = config->out_srid = 0;
    config->nband = NULL;
    config->nband_count = 0;
    memset(config->tile_size, 0, sizeof(int) * 2);
    config->pad_tile = 0;
//    config->hasnodata = 0;
//    config->nodataval = 0;
//    config->transaction = 1;
}

void
rtdealloc_config(RTLOADERCFG *config) {
    int i = 0;
    if (config->rt_file_count) {
        for (i = config->rt_file_count - 1; i >= 0; i--) {
            rtdealloc(config->rt_file[i]);
            if (config->rt_filename)
                rtdealloc(config->rt_filename[i]);
        }
        rtdealloc(config->rt_file);
        if (config->rt_filename)
            rtdealloc(config->rt_filename);
    }
    if (config->schema != NULL)
        rtdealloc(config->schema);
    if (config->table != NULL)
        rtdealloc(config->table);
    if (config->raster_column != NULL)
        rtdealloc(config->raster_column);
    if (config->file_column_name != NULL)
        rtdealloc(config->file_column_name);
//    if (config->overview_count > 0) {
//        if (config->overview != NULL)
//            rtdealloc(config->overview);
//        if (config->overview_table != NULL) {
//            for (i = config->overview_count - 1; i >= 0; i--)
//                rtdealloc(config->overview_table[i]);
//            rtdealloc(config->overview_table);
//        }
//    }
    if (config->nband_count > 0 && config->nband != NULL)
        rtdealloc(config->nband);

    rtdealloc(config);
}


