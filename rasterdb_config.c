//
// Created by 何文婷 on 18/3/22.
//
#include "rasterdb_config.h"

void
init_config(RTLOADERCFG *config) {
    config->rt_file_count = 0;
    config->rt_file = NULL;
    config->schema = "public";
    config->table = "test_raster";
    config->raster_column = "rast";
    config->file_column_name = "filename";
    config->srid = config->out_srid = 0;
    config->nband = NULL;
    config->nband_count = 0;
    memset(config->tile_size, 0, sizeof(int) * 2);
    config->pad_tile = 0;
    config->hasnodata = 0;
    config->nodataval = 0;
//    config->hasnodata = 0;
//    config->nodataval = 0;
//    config->transaction = 1;
}

//int substr(char dst[], char src[],int start, int len) {
//    int i = 0;
//    int j = 0;
//    while(src != NULL && src[i] != '\0' && i < start) i++;
//    while(i < len + start  && src[i] != '\0') {
//        dst[j++] = src[i++] 
//    }
//    dst[j] = '\0'
//}

void
set_config(RTLOADERCFG *config) {
    int MAXSIZE=1024;
    char buf[MAXSIZE];
    char *conf_file="/home/hewenting/casearth/rasterdb/etc/rasterdb.conf";
    FILE *f = fopen(conf_file, "r");

    if (f == NULL) {
        elog(ERROR, "open %s failed. errno=%s", conf_file, strerror(errno));
    }
    while (fgets(buf, MAXSIZE, f) != EOF) {
        char *p = strchr(buf, ':');
        if (p != NULL && strncmp(buf,"tile_size",strlen("tile_size")) == 0) {
            char *p2 = strchr(p, 'x');
            if (p2 != NULL) {
                char s1[5],s2[5];
                strncpy(s1,p+1,p2-p);
                config->tile_size[0] = atoi(s1);
                config->tile_size[1] = atoi(p2+1);
                elog(INFO, "config->tile_size=%dx%d",config->tile_size[0],config->tile_size[1]);
            }
            return;
        }
    }
}

void
rtdealloc_config(RTLOADERCFG *config) {
    int i = 0;
    if (config->rt_file_count) {
        for (i = config->rt_file_count - 1; i >= 0; i--) {
            rtdealloc(config->rt_file[i]);
        }
        rtdealloc(config->rt_file);
    }
//    if (config->schema != NULL)
//        rtdealloc(config->schema);
//    if (config->table != NULL)
//        rtdealloc(config->table);
//    if (config->raster_column != NULL)
//        rtdealloc(config->raster_column);
//    if (config->file_column_name != NULL)
//        rtdealloc(config->file_column_name);
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


