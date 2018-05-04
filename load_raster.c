//
// Created by 何文婷 on 18/3/21.
//
#include "load_raster.h"
#include "ogr_srs_api.h"
#include "gdal_vrt.h"

int analysis_raster(char *filename, RTLOADERCFG *config, int cur_lineno, char **buf) {
    int rows = 0;
    RASTERINFO *rasterinfo;

    elog(DEBUG1, "----->analysis_raster(%s:%d)", filename, cur_lineno);
    rasterinfo = rtalloc(sizeof(RASTERINFO));
    if(rasterinfo == NULL) {
        elog(ERROR, "rtalloc for RASTERINFO failed");
    }
    memset(rasterinfo, 0, sizeof(RASTERINFO));

    /* convert raster to tiles and explained in hexString*/
    rows = convert_raster(filename, config, rasterinfo, cur_lineno, buf);

    elog(DEBUG1, "<-----analysis_raster");
    return rows;
}

int convert_raster(char *filename, RTLOADERCFG *config, RASTERINFO *info, int cur_lineno, char **buf) {
    int ntiles[2] = {1, 1};
    int tileno = 0;
    int processdno = 0;
    int _tile_size[2] = {0};
    int i = 0, xtile = 0, ytile = 0;
    int nbands = 0;
    int batchsize = config->batchsize;
    uint32_t hexlen = 0;
    double gt[6] = {0.};
    char *hex = NULL;
    const char *proDefString = NULL;
    GDALDatasetH hds;
    VRTDatasetH tds;
    VRTSourcedRasterBandH tband;
    rt_raster rast = NULL;

    elog(DEBUG1, "----->convert_raster");
    hds = GDALOpen(filename, GA_ReadOnly);

    //S1: get tilesize
    // dimensions of raster 
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
    if(tileno < cur_lineno)
        return 0;

    //S2: get srs and srid
    proDefString =  GDALGetProjectionRef(hds);
    if (proDefString != NULL && proDefString[0] != '\0') {
        OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);
        //Set info->srs
        info->srs = rtalloc(sizeof(char *) * (strlen(proDefString) + 1));
        if (info->srs == NULL) {
            GDALClose(hds);
            elog(ERROR, "rtalloc for info->srs failed.");
        }
        strcpy(info->srs, proDefString);

        //Set info->srid
        if (OSRSetFromUserInput(hSRS, proDefString) == OGRERR_NONE) {
            const char* pszAuthorityName = OSRGetAuthorityName(hSRS, NULL);
            const char* pszAuthorityCode = OSRGetAuthorityCode(hSRS, NULL);
            if (pszAuthorityName != NULL &&
                    strcmp(pszAuthorityName, "EPSG") == 0 &&
                    pszAuthorityCode != NULL) {
                info->srid = atoi(pszAuthorityCode);
            }
        }
        OSRDestroySpatialReference(hSRS);
    }

    //S2 Set info record geotransform metrix 
    if(GDALGetGeoTransform(hds, info->gt) != CE_None) {
        elog(DEBUG2, "Using default geotransform matrix (0, 1, 0, 0, 0, -1) for raster: %s", filename);
        info->gt[0] = 0;
        info->gt[1] = 1;
        info->gt[2] = 0;
        info->gt[3] = 0;
        info->gt[4] = 0;
        info->gt[5] = -1;
    }
    memcpy(gt, info->gt, sizeof(double) * 6);

    /* 
     * TODO
     * Processing multiple bands 
     */
    nbands = GDALGetRasterCount(hds);
    Assert(nbands == 1);
    info->nband_count = nbands;
    info->nband = rtalloc(info->nband_count * sizeof(int));
    if (info->nband == NULL) {
        GDALClose(hds);
        elog(ERROR, "rtalloc info->nband failed");
    }
    for (i = 0; i < info->nband_count; i++)
        info->nband[i] = i + 1;

    /* initialize parameters dependent on nband */
	info->gdalbandtype = rtalloc(sizeof(GDALDataType) * info->nband_count);
	if (info->gdalbandtype == NULL) {
		GDALClose(hds);
		elog(ERROR, "convert_raster: Could not allocate memory for storing GDAL data type");
	}
	info->bandtype = rtalloc(sizeof(rt_pixtype) * info->nband_count);
	if (info->bandtype == NULL) {
		GDALClose(hds);
		elog(ERROR, "convert_raster: Could not allocate memory for storing pixel type");
	}
    info->hasnodata = rtalloc(sizeof(int) * info->nband_count);
    if (info->hasnodata == NULL) {
	    GDALClose(hds);
	    elog(ERROR, "convert_raster: Could not allocate memory for storing hasnodata flag");
    }
    info->nodataval = rtalloc(sizeof(double) * info->nband_count);
    if (info->nodataval == NULL) {
	    GDALClose(hds);
	    elog(ERROR, "convert_raster: Could not allocate memory for storing nodata value");
    }

    memset(info->gdalbandtype, GDT_Unknown, sizeof(GDALDataType) * info->nband_count);
	memset(info->bandtype, PT_END, sizeof(rt_pixtype) * info->nband_count);
    memset(info->hasnodata, 0, sizeof(int) * info->nband_count);
    memset(info->nodataval, 0, sizeof(double) * info->nband_count);

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

    elog(DEBUG1, "INFO Process each tile %d",__LINE__);
    /* Process each tile */
    for (xtile = cur_lineno / ntiles[1];  xtile < ntiles[0] && processdno < batchsize; xtile++) {
        // x coordinate edge
        if (!config->pad_tile && xtile == ntiles[0] - 1)
            _tile_size[0] = info->dim[0] - xtile * info->tile_size[0];
        else
            _tile_size[0] = info->tile_size[0];

        if(xtile == cur_lineno / ntiles[1])
            ytile = cur_lineno % ntiles[0];
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
            elog(DEBUG1, "xtile=%d,ytile=%d,info->tile_size=%dx%d,_tile_size=%dx%d",xtile,ytile,info->tile_size[0],
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

            hex = rt_raster_to_hexwkb(rast, FALSE, &hexlen);
            raster_destroy(rast);

            if (hex == NULL) {
                GDALClose(hds);
                elog(ERROR, "rt_raster_to_hexwk return NULL.");
            }
            GDALClose(tds);

            buf[processdno++] = hex; 
            if(processdno == batchsize)
                break;
        }
    }// finish process all the tiles

    GDALClose(hds);
    elog(DEBUG1, "<----->convert_raster");
    return processdno;
}
