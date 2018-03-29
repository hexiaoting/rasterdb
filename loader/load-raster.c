//
// Created by 何文婷 on 18/3/21.
//
#include "load-raster.h"
//#include "gdal.h"
#include "ogr_srs_api.h"
//#include "cpl_conv.h" /* for CPLMalloc() */
#include "gdal_vrt.h"

void analysis_raster(RTLOADERCFG *config) {
    int i = 0;
    GDALDriverH drv = NULL;
    STRINGBUFFER *buffer = NULL;

    GDALAllRegister();
    /* check that GDAL recognizes all files */
    for (i = 0; i < config->rt_file_count; i++) {
        drv = GDALIdentifyDriver(config->rt_file[i], NULL);

        if (drv == NULL) {
            rterror(("Unable to read raster file: %s"), config->rt_file[i]);
            rtdealloc_config(config);
            exit(1);
        }
    }

    /* initialize string buffer */
    buffer = rtalloc(sizeof(STRINGBUFFER));
    if (buffer == NULL) {
        rterror("Could not allocate memory for output string buffer");
        rtdealloc_config(config);
        exit(1);
    }

    buffer->length = 0;
    buffer->line = NULL;

    process_rasters(config, buffer);
    rtdealloc_stringbuffer(buffer, 1);
    rtdealloc_config(config);


}

int
process_rasters(RTLOADERCFG *config, STRINGBUFFER *buffer) {
    int i = 0;
    create_table(config, buffer);

    /* process each raster */
    for (i = 0; i < config->rt_file_count; i++) {
        STRINGBUFFER tileset;
        RASTERINFO *rasterinfo = rtalloc(sizeof(RASTERINFO));
        char *filename = NULL;

        if(rasterinfo == NULL) {
            rterror("error");
            return 0;
        }

        tileset.length = 0;
        tileset.line = NULL;

        /* convert raster */
        if (!convert_raster(i, config, rasterinfo, &tileset, buffer)){
            rtdealloc_stringbuffer(&tileset, 1);
            return 0;
        }
        if(tileset.length) {
            if(!insert_records(filename, config->schema,config->table,
                           config->raster_column, config->rt_file[i],
                           config->out_srid,
                           &tileset, buffer)){
                rterror("err");
                return 1;
            }
        }

        rtdealloc_stringbuffer(&tileset, 0);
    }
    return 1;
}

int convert_raster(int idx, RTLOADERCFG *config, RASTERINFO *info, STRINGBUFFER *tileset, STRINGBUFFER *buffer) {
    GDALDatasetH hds;
    const char *proDefString = NULL;
    double gt[6] = {0.};
    int ntiles[2] = {1, 1};
    int _tile_size[2] = {0};
    int i = 0, xtile = 0, ytile = 0;
    rt_raster rast = NULL;
    char *hex = NULL;
    uint32_t hexlen = 0;
    int nbands = 0;
    //int tilesize = 0;

    hds = GDALOpen(config->rt_file[idx], GA_ReadOnly);
    nbands = GDALGetRasterCount(hds);
    if (nbands != 1) {
        rterror(("Open raster file %s ,bandNum = %d"),config->rt_file[idx], nbands);
        GDALClose(hds);
        return 0;
    }

    // get srs and srid
    proDefString =  GDALGetProjectionRef(hds);
    if (proDefString != NULL && proDefString[0] != '\0') {
        info->srs = rtalloc(sizeof(char *) * (strlen(proDefString) + 1));
        if (info->srs == NULL) {
            rterror("alloc failed.");
            GDALClose(hds);
            return 0;
        }
        strcpy(info->srs, proDefString);
        if (info->srid == 0) {
            OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);
            if (OSRSetFromUserInput(hSRS, proDefString) == OGRERR_NONE) {
                const char* pszAuthorityName = OSRGetAuthorityName(hSRS, NULL);
                const char* pszAuthorityCode = OSRGetAuthorityCode(hSRS, NULL);
                if (
                        pszAuthorityName != NULL &&
                        strcmp(pszAuthorityName, "EPSG") == 0 &&
                        pszAuthorityCode != NULL
                        ) {
                    info->srid = atoi(pszAuthorityCode);
                }
            }
            OSRDestroySpatialReference(hSRS);
        }
    }

    /* record geotransform metrix */
    if(GDALGetGeoTransform(hds, info->gt) != CE_None) {
        rtinfo(("Using default geotransform matrix (0, 1, 0, 0, 0, -1) for raster: %s"), config->rt_file[idx]);
        info->gt[0] = 0;
        info->gt[1] = 1;
        info->gt[2] = 0;
        info->gt[3] = 0;
        info->gt[4] = 0;
        info->gt[5] = -1;
    }
    memcpy(gt, info->gt, sizeof(double) * 6);

    /* TODO
     * multi bands */
    info->nband_count = nbands;
    info->nband = rtalloc(info->nband_count * sizeof(int));
    if (info->nband == NULL) {
        rterror("errr");
        GDALClose(hds);
        return 0;
    }
    for (i = 0; i < info->nband_count; i++)
        info->nband[i] = i + 1;

    /* dimensions of raster */
    info->dim[0] = GDALGetRasterXSize(hds);
    info->dim[1] = GDALGetRasterYSize(hds);

    /* tile split:
     * if no tile size set, then reuse orignal raster dimensions
     * eg: raster dimenstion = 400x400
     *       tile dimenstion = 100x100
     *       then there where be 160000/10000 = 16 number of tiles
     * */
    info->tile_size[0] = (config->tile_size[0] ? config->tile_size[0] : info->dim[0]);
    info->tile_size[1] = (config->tile_size[1] ? config->tile_size[1] : info->dim[1]);
    // number of tiles on width and height
    if (config->tile_size[0] != info->dim[0])
        ntiles[0] = (info->dim[0] + info->tile_size[0] - 1)/(info->tile_size[0]);
    if (config->tile_size[1] != info->dim[1])
        ntiles[1] = (info->dim[1] + info->tile_size[1] - 1)/(info->tile_size[1]);

    /*TODO:
     * estimate tilesize exceed
     */
    //tilesize = info->tile_size[0] * info->tile_size[1];

    /* Process each band data type*/
    for (i = 0; i < info->nband_count; i++) {
        GDALRasterBandH rbh = GDALGetRasterBand(hds, info->nband[i]);
        info->gdalbandtype[i] = GDALGetRasterDataType(rbh);
        info->bandtype[i] = rt_util_gdal_datatype_to_pixtype(info->gdalbandtype[i]);
    }

    /* Process each tile */
    for (xtile = 0;  xtile < ntiles[0]; xtile++) {
        // x coordinate edge
        if (!config->pad_tile && xtile == ntiles[0] - 1)
            _tile_size[0] = info->dim[0] - xtile * info->tile_size[0];
        else
            _tile_size[0] = info->tile_size[0];
        for (ytile = 0; ytile < ntiles[1]; ytile++) {
            VRTDatasetH tds;
            // y coordinate edge
            if (!config->pad_tile && ytile == ntiles[1] - 1)
                _tile_size[1] = info->dim[1] - ytile * info->tile_size[1];
            else
                _tile_size[1] = info->tile_size[1];

            //Create tile
            tds = VRTCreate(_tile_size[0], _tile_size[1]);
            GDALSetProjection(tds, info->srs);
            //TODO: DO not understand why
            //GDALApplyGeoTransform(info->gt, xtile * info->tile_size[0], ytile * info->tile_size[1], &(gt[0]),&(gt[1]));
            GDALSetGeoTransform(tds, gt);

            //Add band data sources
            for (i = 0; i < info->nband_count; i++) {
                VRTSourcedRasterBandH tband;
                GDALAddBand(tds, info->gdalbandtype[i], NULL);
                tband = (VRTSourcedRasterBandH)GDALGetRasterBand(tds, i + 1);
                VRTAddSimpleSource(tband,hds,
                                   xtile * info->tile_size[0],ytile * info->tile_size[1],
                                   _tile_size[0],_tile_size[1],
                                   0,0,
                                   _tile_size[0],_tile_size[1],
                                    "near", VRT_NODATA_UNSET);

            }

            VRTFlushCache(tds);

            /* Convert VRT to raster to hexwkb
             */
            rast = rt_raster_from_gdal_dataset(tds);
            if (rast == NULL) {
                rterror("error");
                GDALClose(hds);
                return 0;
            }

            rt_raster_set_srid(rast, info->srid);

            //TODO: check echo band of no data

            hex = rt_raster_to_hexwkb(rast, FALSE, &hexlen);
            raster_destroy(rast);

            if (hex == NULL) {
                rterror("err");
                GDALClose(hds);
                return 0;
            }

            /* add hexwkb to tileset */
            append_stringbuffer(tileset, hex);

            GDALClose(tds);
        }
    }// finish process all the tiles


    GDALClose(hds);
    return 1;
}

int create_table(RTLOADERCFG *config, STRINGBUFFER *buffer) {
    char *create_sql = NULL;
    int length = strlen("CREATE TABLE  (rid serial primary key, raster);") + 1;
    length += strlen(config->schema) + strlen(config->table) + strlen(config->raster_column);
    if (config->file_column_name)
        length += strlen(", text") + strlen(config->file_column_name);

    create_sql = rtalloc(length * sizeof(char));
    if (create_sql == NULL) {
        rterror("err");
        return 0;
    }

    sprintf(create_sql, "CREATE TABLE %s.%s(\"rid\" serial primary key, %s raster%s%s%s)",
             config->schema,config->table,config->raster_column,
             (config->file_column_name == NULL ? "" : ","),
             (config->file_column_name == NULL ? "" : config->file_column_name),
             (config->file_column_name == NULL ? "" : " text"));

    append_stringbuffer(buffer, create_sql);
    return 1;
}

int insert_records(char *filename, char *schema, char *table, char *rast_column, char *file_column_name,
                   int out_srid,
                   STRINGBUFFER *tileset, STRINGBUFFER *buffer){
    int length = 0;
    int i = 0;

    for (i = 0; i < tileset->length; i++) {
        char *sql = rtalloc(sizeof(char) * length);
        char *ptr = NULL;
        if (sql == NULL) {
            rterror("err");
            return 1;
        }
        ptr = sql;
        ptr += sprintf(sql, "INSERT INTO %s.%s(%s%s%s) values(",
                schema,table,
                rast_column,
                filename ? "," : "",
                filename ? file_column_name : "");
        if (out_srid != 0) {
            ptr += sprintf(ptr, "ST_Transform(");
        }
        ptr += sprintf(ptr, "%s::raster", tileset->line[i]);
        if (out_srid != 0) {
            ptr += sprintf(ptr, ")");
        }
        if(filename != NULL) {
            ptr += sprintf(ptr, ",%s",filename);
        }
        ptr += sprintf(ptr, ");");
        append_stringbuffer(buffer, sql);
        rtdealloc(sql);
        sql = NULL;
    }
    return 0;
}



int append_stringbuffer(STRINGBUFFER *buffer, const char *str) {
    buffer->length++;
    buffer->line = rtrealloc(buffer->line, buffer->length);
    if (buffer->line == NULL) {
        rterror("Failed to allocate buffer->line");
        return 0;
    }
    buffer->line[buffer->length - 1] = (char *)str;
    return 1;
}

void rtdealloc_stringbuffer(STRINGBUFFER *buffer, int freebuffer) {
    uint32_t i = 0;
    for (i = 0; i < buffer->length; i++) {
        if(buffer->line[i] != NULL)
            rtdealloc(buffer->line[i]);
    }
    buffer->line = NULL;
    buffer->length = 0;

    if(freebuffer)
        rtdealloc(buffer);
}
