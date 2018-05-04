//
// Created by 何文婷 on 18/3/22.
//
#include "rasterdb_config.h"
#include <string.h>

void init_config(RTLOADERCFG *config) {
    config->rt_file_count = 0;
    config->rt_file = NULL;
    config->srid = config->out_srid = 0;
    config->batchsize = DEFAULT_BATCHSIZE;
    config->nband = NULL;
    config->nband_count = 0;
    memset(config->tile_size, 0, sizeof(int) * 2);
    config->pad_tile = 0;
    config->hasnodata = 0;
    config->nodataval = 0;
    config->location = rtalloc(LOCATION_MAXSIZE);
    if (config->location == NULL) {
        elog(ERROR, "alloc memory for config->location failed.");
    }
    memset(config->location, 0, sizeof(char) * LOCATION_MAXSIZE);
}

void set_config(RTLOADERCFG **config, char *conf_file) {
    int MAXSIZE=1024;
    char buf[MAXSIZE];
    FILE *f = NULL;

    //S1: set config
    *config = rtalloc(sizeof(RTLOADERCFG));
    if (*config == NULL) {
        elog(ERROR, "rtalloc RTLOADERCFG failed");
    }
    init_config(*config);

    Assert(conf_file != NULL);
    f = fopen(conf_file, "r");
    if (f == NULL) {
        elog(ERROR, "open %s failed. errno=%s", conf_file, strerror(errno));
    }
    memset(buf, 0, MAXSIZE);
    while (fgets(buf, MAXSIZE, f) != NULL) {
        char *p = strchr(buf, '=');
        if (p == NULL) {
            elog(INFO, "conf_file setting %s failed(without '=').", buf);
        }
        else if (strncmp(buf,"tile_size",strlen("tile_size")) == 0) {
            char *p2 = strchr(p, 'x');
            if (p2 != NULL) {
                char s1[5];
                strncpy(s1,p+1,p2-p);
                (*config)->tile_size[0] = atoi(s1);
                (*config)->tile_size[1] = atoi(p2+1);
                elog(DEBUG1, "config->tile_size=%dx%d",(*config)->tile_size[0],(*config)->tile_size[1]);
            }
        } else if(strncmp(buf, "location", strlen("location")) == 0) {
            strncpy((*config)->location, p + 1, strlen(p + 1) - 1);
            elog(DEBUG1, "config->location = %s", (*config)->location);
        } else if(strncmp(buf, "batchsize", strlen("batchsize")) == 0) {
            (*config)->batchsize = atoi(p + 1);
            elog(DEBUG1, "config->batchsize= %d", (*config)->batchsize);
        }
    }
    fclose(f);
}

void rtdealloc_config(RTLOADERCFG *config) {
    int i = 0;
    if (config->rt_file_count) {
        for (i = config->rt_file_count - 1; i >= 0; i--) {
            rtdealloc(config->rt_file[i]);
        }
        rtdealloc(config->rt_file);
    }
    if (config->nband_count > 0 && config->nband != NULL)
        rtdealloc(config->nband);
    rtdealloc(config->location);

    rtdealloc(config);
}
