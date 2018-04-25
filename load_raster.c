//
// Created by 何文婷 on 18/3/21.
//
#include "load_raster.h"

#include "ogr_srs_api.h"
//#include "cpl_conv.h" /* for CPLMalloc() */
#include "gdal_vrt.h"

int analysis_raster(char *filename, RTLOADERCFG *config, int cur_linno, char **buf) {
    int rows = 0;
    //STRINGBUFFER *buffer = NULL;

    elog(INFO, "----->%s:%d", __FILE__, __LINE__);

    //process_rasters(config, buffer);
    rows = process_rasters(filename, config, cur_linno, buf);

    elog(INFO, "<-----analysis_raster");
    return rows;
}

/*
 * Return: get lines
 */
int
//process_rasters(RTLOADERCFG *config, STRINGBUFFER *buffer) {
process_rasters(char *filename, RTLOADERCFG *config, int cur_linno, char **buf) {
    int lines = 0;
    RASTERINFO *rasterinfo;

    /* process each raster */
    elog(INFO, "----->%s:%d", __FILE__, __LINE__);

    rasterinfo = rtalloc(sizeof(RASTERINFO));
    if(rasterinfo == NULL) {
        rterror("error");
        return 0;
    }
    memset(rasterinfo, 0, sizeof(RASTERINFO));


    /* convert raster */
    //TODO: multi files
    lines += convert_raster(filename, config, rasterinfo, cur_linno, buf);

    elog(INFO, "----->%s:%d", __FILE__, __LINE__);
    return lines;
}

//int convert_raster(int idx, RTLOADERCFG *config, RASTERINFO *info, STRINGBUFFER *tileset, STRINGBUFFER *buffer) {
int convert_raster(char *filename, RTLOADERCFG *config, RASTERINFO *info, int cur_linno, char **buf) {
    GDALDatasetH hds;
    const char *proDefString = NULL;
    double gt[6] = {0.};
    int ntiles[2] = {1, 1};
    int tileno = 0;
    int processdno = 0;
    int _tile_size[2] = {0};
    int i = 0, xtile = 0, ytile = 0;
    rt_raster rast = NULL;
    char *hex = NULL;
    uint32_t hexlen = 0;
    int nbands = 0;
    VRTDatasetH tds;
    VRTSourcedRasterBandH tband;
    int batchsize = config->batchsize;
    //int tilesize = 0;

    elog(INFO, "----->%s convert_raster:%d", __FILE__, __LINE__);
    hds = GDALOpen(filename, GA_ReadOnly);
    nbands = GDALGetRasterCount(hds);
    if (nbands != 1) {
        elog(ERROR, "ERROR----->%s:%d", __FILE__, __LINE__);
        rterror(("Open raster file %s ,bandNum = %d"), filename, nbands);
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
        rtinfo(("Using default geotransform matrix (0, 1, 0, 0, 0, -1) for raster: %s"), filename);
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

    /* initialize parameters dependent on nband */
	info->gdalbandtype = rtalloc(sizeof(GDALDataType) * info->nband_count);
	if (info->gdalbandtype == NULL) {
		rterror(_("convert_raster: Could not allocate memory for storing GDAL data type"));
		GDALClose(hds);
		return 0;
	}
	info->bandtype = rtalloc(sizeof(rt_pixtype) * info->nband_count);
	if (info->bandtype == NULL) {
		rterror(_("convert_raster: Could not allocate memory for storing pixel type"));
		GDALClose(hds);
		return 0;
	}
    info->hasnodata = rtalloc(sizeof(int) * info->nband_count);
    if (info->hasnodata == NULL) {
	    rterror(_("convert_raster: Could not allocate memory for storing hasnodata flag"));
	    GDALClose(hds);
	    return 0;
    }
    info->nodataval = rtalloc(sizeof(double) * info->nband_count);
    if (info->nodataval == NULL) {
	    rterror(_("convert_raster: Could not allocate memory for storing nodata value"));
	    GDALClose(hds);
	    return 0;
    }

    memset(info->gdalbandtype, GDT_Unknown, sizeof(GDALDataType) * info->nband_count);
	memset(info->bandtype, PT_END, sizeof(rt_pixtype) * info->nband_count);
    memset(info->hasnodata, 0, sizeof(int) * info->nband_count);
    memset(info->nodataval, 0, sizeof(double) * info->nband_count);

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

    tileno = ntiles[0] * ntiles[1];
    if(tileno < cur_linno)
        return 0;

    /*TODO:
     * estimate tilesize exceed
     */
    //tilesize = info->tile_size[0] * info->tile_size[1];

    /* Process each band data type*/
    for (i = 0; i < info->nband_count; i++) {
        GDALRasterBandH rbh = GDALGetRasterBand(hds, info->nband[i]);
        info->gdalbandtype[i] = GDALGetRasterDataType(rbh);
        info->bandtype[i] = rt_util_gdal_datatype_to_pixtype(info->gdalbandtype[i]);
        
        /* hasnodata and nodataval*/
        info->nodataval[i] = GDALGetRasterNoDataValue(rbh, &(info->hasnodata[i]));
        if (!info->hasnodata[i]) {
            if(config->hasnodata) {
                info->hasnodata[i] = 1;
                info->nodataval[i] = config->nodataval;
            } else {
                info->nodataval[i] = 0;
            }
        }
    }

    elog(INFO, "INFO Process each tile %d",__LINE__);
    /* Process each tile */
    for (xtile = cur_linno / ntiles[1];  xtile < ntiles[0] && processdno < batchsize; xtile++) {
        // x coordinate edge
        if (!config->pad_tile && xtile == ntiles[0] - 1)
            _tile_size[0] = info->dim[0] - xtile * info->tile_size[0];
        else
            _tile_size[0] = info->tile_size[0];

        if(xtile == cur_linno / ntiles[1])
            ytile = cur_linno % ntiles[0];
        else 
            ytile = 0;

        for (; ytile < ntiles[1]; ytile++) {
            // y coordinate edge
            if (!config->pad_tile && ytile == ntiles[1] - 1)
                _tile_size[1] = info->dim[1] - ytile * info->tile_size[1];
            else
                _tile_size[1] = info->tile_size[1];


            //Create tile
            tds = VRTCreate(_tile_size[0], _tile_size[1]);
            GDALSetProjection(tds, info->srs);
            //TODO: DO not understand why
            GDALApplyGeoTransform(info->gt, xtile * info->tile_size[0], ytile * info->tile_size[1], &(gt[0]),&(gt[3]));
            GDALSetGeoTransform(tds, gt);

            //Add band data sources
            elog(INFO, "xtile=%d,ytile=%d,info->tile_size=%dx%d,_tile_size=%dx%d",xtile,ytile,info->tile_size[0],
			info->tile_size[1],_tile_size[0],_tile_size[1]);
            for (i = 0; i < info->nband_count; i++) {
                GDALAddBand(tds, info->gdalbandtype[i], NULL);
                tband = (VRTSourcedRasterBandH)GDALGetRasterBand(tds, i + 1);
                if (info->hasnodata[i])
                    GDALSetRasterNoDataValue(tband, info->nodataval[i]);
                VRTAddSimpleSource(tband, GDALGetRasterBand(hds, info->nband[i]),
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
                elog(ERROR, "rt_raster_from_gdal_dataset failed.");
                rterror("error");
                GDALClose(hds);
                return 0;
            }

            rt_raster_set_srid(rast, info->srid);

            //TODO: check echo band of no data

            hex = rt_raster_to_hexwkb(rast, FALSE, &hexlen);
            raster_destroy(rast);

            if (hex == NULL) {
                elog(ERROR, "rt_raster_to_hexwk return NULL.");
                rterror("err");
                GDALClose(hds);
                return 0;
            }
            GDALClose(tds);

            buf[processdno++] = hex; 
            if(processdno == batchsize)
                break;
            /* add hexwkb to tileset */
            //append_stringbuffer(tileset, hex);

        }
    }// finish process all the tiles


    GDALClose(hds);
    elog(INFO, "----->%s:%d", __FILE__, __LINE__);
    return processdno;
}

int append_stringbuffer(STRINGBUFFER *buffer, const char *str) {
    buffer->length++;
    buffer->line = rtrealloc(buffer->line, sizeof(char *) * buffer->length);
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
        //elog(INFO, "rtdealloc_stringbuffer i=%d,line=%s",i,buffer->line[i]);
        if(buffer->line[i] != NULL)
            rtdealloc(buffer->line[i]);
    }
    elog(INFO, "line358");
    buffer->line = NULL;
    buffer->length = 0;

    if(freebuffer) {
        rtdealloc(buffer);
        elog(INFO, "line364");
    }
}
