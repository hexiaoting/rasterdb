//
// Created by 何文婷 on 18/3/22.
//
#include "librtcore.h"
#include "stdint.h"
#include "rt_context.h"
#include <assert.h>
#include "ogr_srs_api.h"

rt_pixtype
rt_util_gdal_datatype_to_pixtype(GDALDataType gdt) {
    switch (gdt) {
        case GDT_Byte:
            return PT_8BUI;
        case GDT_UInt16:
            return PT_16BUI;
        case GDT_Int16:
            return PT_16BSI;
        case GDT_UInt32:
            return PT_32BUI;
        case GDT_Int32:
            return PT_32BSI;
        case GDT_Float32:
            return PT_32BF;
        case GDT_Float64:
            return PT_64BF;
        default:
            return PT_END;
    }

    return PT_END;
}

void *
rt_band_get_data(rt_band band) {
    assert(NULL != band);

    if (band->offline) {
        if (band->data.offline.mem != NULL)
            return band->data.offline.mem;

        if (rt_band_load_offline_data(band) != 0)
            return NULL;
        else
            return band->data.offline.mem;
    }
    else
        return band->data.mem;
}

int
rt_band_get_ownsdata_flag(rt_band band) {
    assert(NULL != band);
    return band->ownsdata ? 1 : 0;
}

void
rt_raster_destroy(rt_raster raster) {
    if (raster == NULL)
        return;

//    RASTER_DEBUGF(3, "Destroying rt_raster @ %p", raster);

    if (raster->bands)
        rtdealloc(raster->bands);

    rtdealloc(raster);
}

void
raster_destroy(rt_raster raster) {
    uint16_t i;
    uint16_t nbands = rt_raster_get_num_bands(raster);
    for (i = 0; i < nbands; i++) {
        rt_band band = rt_raster_get_band(raster, i);
        if (band == NULL) continue;

        if (!rt_band_is_offline(band) && !rt_band_get_ownsdata_flag(band)) {
            void* mem = rt_band_get_data(band);
            if (mem) rtdealloc(mem);
        }
        rt_band_destroy(band);
    }
    rt_raster_destroy(raster);
}

rt_raster
rt_raster_new(uint32_t width, uint32_t height) {
    rt_raster ret = NULL;

    ret = (rt_raster) rtalloc(sizeof (struct rt_raster_t));
    if (!ret) {
        rterror("rt_raster_new: Out of virtual memory creating an rt_raster");
        return NULL;
    }

//    RASTER_DEBUGF(3, "Created rt_raster @ %p", ret);

    if (width > 65535 || height > 65535) {
        rterror("rt_raster_new: Dimensions requested exceed the maximum (65535 x 65535) permitted for a raster");
        rt_raster_destroy(ret);
        return NULL;
    }

    ret->width = width;
    ret->height = height;
    ret->scaleX = 1;
    ret->scaleY = -1;
    ret->ipX = 0.0;
    ret->ipY = 0.0;
    ret->skewX = 0.0;
    ret->skewY = 0.0;
    ret->srid = 0;

    ret->numBands = 0;
    ret->bands = NULL;

    return ret;
}

rt_errorstate
rt_util_gdal_sr_auth_info(GDALDatasetH hds, char **authname, char **authcode) {
    const char *srs = NULL;

    assert(authname != NULL);
    assert(authcode != NULL);

    *authname = NULL;
    *authcode = NULL;

    srs = GDALGetProjectionRef(hds);
    if (srs != NULL && srs[0] != '\0') {
        OGRSpatialReferenceH hSRS = OSRNewSpatialReference(NULL);

        if (OSRSetFromUserInput(hSRS, srs) == OGRERR_NONE) {
            const char* pszAuthorityName = OSRGetAuthorityName(hSRS, NULL);
            const char* pszAuthorityCode = OSRGetAuthorityCode(hSRS, NULL);

            if (pszAuthorityName != NULL && pszAuthorityCode != NULL) {
                *authname = rtalloc(sizeof(char) * (strlen(pszAuthorityName) + 1));
                *authcode = rtalloc(sizeof(char) * (strlen(pszAuthorityCode) + 1));

                if (*authname == NULL || *authcode == NULL) {
                    rterror("rt_util_gdal_sr_auth_info: Could not allocate memory for auth name and code");
                    if (*authname != NULL) rtdealloc(*authname);
                    if (*authcode != NULL) rtdealloc(*authcode);
                    OSRDestroySpatialReference(hSRS);
                    return ES_ERROR;
                }

                strncpy(*authname, pszAuthorityName, strlen(pszAuthorityName) + 1);
                strncpy(*authcode, pszAuthorityCode, strlen(pszAuthorityCode) + 1);
            }
        }

        OSRDestroySpatialReference(hSRS);
    }

    return ES_NONE;
}

int
rt_raster_generate_new_band(
        rt_raster raster, rt_pixtype pixtype,
        double initialvalue, uint32_t hasnodata, double nodatavalue,
        int index
) {
    rt_band band = NULL;
    int width = 0;
    int height = 0;
    int numval = 0;
    int datasize = 0;
    int oldnumbands = 0;
    int numbands = 0;
    void * mem = NULL;
    int i;


    assert(NULL != raster);

    /* Make sure index is in a valid range */
    oldnumbands = rt_raster_get_num_bands(raster);
    if (index < 0)
        index = 0;
    else if (index > oldnumbands + 1)
        index = oldnumbands + 1;

    /* Determine size of memory block to allocate and allocate it */
    width = rt_raster_get_width(raster);
    height = rt_raster_get_height(raster);
    numval = width * height;
    datasize = rt_pixtype_size(pixtype) * numval;

    mem = (int *)rtalloc(datasize);
    if (!mem) {
        rterror("rt_raster_generate_new_band: Could not allocate memory for band");
        return -1;
    }

    if (FLT_EQ(initialvalue, 0.0))
        memset(mem, 0, datasize);
    else {
        switch (pixtype)
        {
            case PT_1BB:
            {
                uint8_t *ptr = mem;
                uint8_t clamped_initval = rt_util_clamp_to_1BB(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_2BUI:
            {
                uint8_t *ptr = mem;
                uint8_t clamped_initval = rt_util_clamp_to_2BUI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_4BUI:
            {
                uint8_t *ptr = mem;
                uint8_t clamped_initval = rt_util_clamp_to_4BUI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_8BSI:
            {
                int8_t *ptr = mem;
                int8_t clamped_initval = rt_util_clamp_to_8BSI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_8BUI:
            {
                uint8_t *ptr = mem;
                uint8_t clamped_initval = rt_util_clamp_to_8BUI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_16BSI:
            {
                int16_t *ptr = mem;
                int16_t clamped_initval = rt_util_clamp_to_16BSI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_16BUI:
            {
                uint16_t *ptr = mem;
                uint16_t clamped_initval = rt_util_clamp_to_16BUI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_32BSI:
            {
                int32_t *ptr = mem;
                int32_t clamped_initval = rt_util_clamp_to_32BSI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_32BUI:
            {
                uint32_t *ptr = mem;
                uint32_t clamped_initval = rt_util_clamp_to_32BUI(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_32BF:
            {
                float *ptr = mem;
                float clamped_initval = rt_util_clamp_to_32F(initialvalue);
                for (i = 0; i < numval; i++)
                    ptr[i] = clamped_initval;
                break;
            }
            case PT_64BF:
            {
                double *ptr = mem;
                for (i = 0; i < numval; i++)
                    ptr[i] = initialvalue;
                break;
            }
            default:
            {
                rterror("rt_raster_generate_new_band: Unknown pixeltype %d", pixtype);
                rtdealloc(mem);
                return -1;
            }
        }
    }


    band = rt_band_new_inline(width, height, pixtype, hasnodata, nodatavalue, mem);
    if (! band) {
        rterror("rt_raster_generate_new_band: Could not add band to raster. Aborting");
        rtdealloc(mem);
        return -1;
    }
    rt_band_set_ownsdata_flag(band, 1); /* we DO own this data!!! */
    index = rt_raster_add_band(raster, band, index);
    numbands = rt_raster_get_num_bands(raster);
    if (numbands == oldnumbands || index == -1) {
        rterror("rt_raster_generate_new_band: Could not add band to raster. Aborting");
        rt_band_destroy(band);
    }

    /* set isnodata if hasnodata = TRUE and initial value = nodatavalue */
    if (hasnodata && FLT_EQ(initialvalue, nodatavalue))
        rt_band_set_isnodata_flag(band, 1);

    return index;
}

rt_raster
rt_raster_from_gdal_dataset(GDALDatasetH ds) {
    rt_raster rast = NULL;
    double gt[6] = {0};
    CPLErr cplerr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t numBands = 0;
    int i = 0;
    char *authname = NULL;
    char *authcode = NULL;

    GDALRasterBandH gdband = NULL;
    GDALDataType gdpixtype = GDT_Unknown;
    rt_band band;
    int32_t idx;
    rt_pixtype pt = PT_END;
    uint32_t ptlen = 0;
    int hasnodata = 0;
    double nodataval;

    int x;
    int y;

    int nXBlocks, nYBlocks;
    int nXBlockSize, nYBlockSize;
    int iXBlock, iYBlock;
    int nXValid, nYValid;
    int iY;

    uint8_t *values = NULL;
    uint32_t valueslen = 0;
    uint8_t *ptr = NULL;


    /* raster size */
    width = GDALGetRasterXSize(ds);
    height = GDALGetRasterYSize(ds);

    /* create new raster */
    rast = rt_raster_new(width, height);
    if (NULL == rast) {
        rterror("rt_raster_from_gdal_dataset: Out of memory allocating new raster");
        return NULL;
    }
//    RASTER_DEBUGF(3, "Created raster dimensions (width x height): %d x %d", rast->width, rast->height);

    /* get raster attributes */
    cplerr = GDALGetGeoTransform(ds, gt);
    if (cplerr != CE_None) {
//        RASTER_DEBUG(4, "Using default geotransform matrix (0, 1, 0, 0, 0, -1)");
        gt[0] = 0;
        gt[1] = 1;
        gt[2] = 0;
        gt[3] = 0;
        gt[4] = 0;
        gt[5] = -1;
    }

    /* apply raster attributes */
    rast->ipX = gt[0];
    rast->scaleX = gt[1];
    rast->skewX = gt[2];
    rast->ipY = gt[3];
    rast->skewY = gt[4];
    rast->scaleY = gt[5];

    /* srid */
    if (rt_util_gdal_sr_auth_info(ds, &authname, &authcode) == ES_NONE) {
        if (
                authname != NULL &&
                strcmp(authname, "EPSG") == 0 &&
                authcode != NULL
                ) {
            rt_raster_set_srid(rast, atoi(authcode));
        }

        if (authname != NULL)
            rtdealloc(authname);
        if (authcode != NULL)
            rtdealloc(authcode);
    }

    numBands = GDALGetRasterCount(ds);


    /* copy bands */
    for (i = 1; i <= numBands; i++) {
        gdband = NULL;
        gdband = GDALGetRasterBand(ds, i);

        /* pixtype */
        gdpixtype = GDALGetRasterDataType(gdband);
        pt = rt_util_gdal_datatype_to_pixtype(gdpixtype);
        if (pt == PT_END) {
            rterror("rt_raster_from_gdal_dataset: Unknown pixel type for GDAL band");
            rt_raster_destroy(rast);
            return NULL;
        }
        ptlen = rt_pixtype_size(pt);

        /* size: width and height */
        width = GDALGetRasterBandXSize(gdband);
        height = GDALGetRasterBandYSize(gdband);
//        RASTER_DEBUGF(3, "GDAL band dimensions (width x height): %d x %d", width, height);

        /* nodata */
        nodataval = GDALGetRasterNoDataValue(gdband, &hasnodata);
//        RASTER_DEBUGF(3, "(hasnodata, nodataval) = (%d, %f)", hasnodata, nodataval);

        /* create band object */
        idx = rt_raster_generate_new_band(
                rast, pt,
                (hasnodata ? nodataval : 0),
                hasnodata, nodataval, rt_raster_get_num_bands(rast)
        );
        if (idx < 0) {
            rterror("rt_raster_from_gdal_dataset: Could not allocate memory for raster band");
            rt_raster_destroy(rast);
            return NULL;
        }
        band = rt_raster_get_band(rast, idx);
//        RASTER_DEBUGF(3, "Created band of dimension (width x height): %d x %d", band->width, band->height);

        /* this makes use of GDAL's "natural" blocks */
        GDALGetBlockSize(gdband, &nXBlockSize, &nYBlockSize);
        nXBlocks = (width + nXBlockSize - 1) / nXBlockSize;
        nYBlocks = (height + nYBlockSize - 1) / nYBlockSize;
//        RASTER_DEBUGF(4, "(nXBlockSize, nYBlockSize) = (%d, %d)", nXBlockSize, nYBlockSize);
//        RASTER_DEBUGF(4, "(nXBlocks, nYBlocks) = (%d, %d)", nXBlocks, nYBlocks);

        /* allocate memory for values */
        valueslen = ptlen * nXBlockSize * nYBlockSize;
        values = rtalloc(valueslen);
        if (values == NULL) {
            rterror("rt_raster_from_gdal_dataset: Could not allocate memory for GDAL band pixel values");
            rt_raster_destroy(rast);
            return NULL;
        }
//        RASTER_DEBUGF(3, "values @ %p of length = %d", values, valueslen);

        for (iYBlock = 0; iYBlock < nYBlocks; iYBlock++) {
            for (iXBlock = 0; iXBlock < nXBlocks; iXBlock++) {
                x = iXBlock * nXBlockSize;
                y = iYBlock * nYBlockSize;
//                RASTER_DEBUGF(4, "(iXBlock, iYBlock) = (%d, %d)", iXBlock, iYBlock);
//                RASTER_DEBUGF(4, "(x, y) = (%d, %d)", x, y);

                memset(values, 0, valueslen);

                /* valid block width */
                if ((iXBlock + 1) * nXBlockSize > width)
                    nXValid = width - (iXBlock * nXBlockSize);
                else
                    nXValid = nXBlockSize;

                /* valid block height */
                if ((iYBlock + 1) * nYBlockSize > height)
                    nYValid = height - (iYBlock * nYBlockSize);
                else
                    nYValid = nYBlockSize;

//                RASTER_DEBUGF(4, "(nXValid, nYValid) = (%d, %d)", nXValid, nYValid);

                cplerr = GDALRasterIO(
                        gdband, GF_Read,
                        x, y,
                        nXValid, nYValid,
                        values, nXValid, nYValid,
                        gdpixtype,
                        0, 0
                );
                if (cplerr != CE_None) {
                    rterror("rt_raster_from_gdal_dataset: Could not get data from GDAL raster");
                    rtdealloc(values);
                    rt_raster_destroy(rast);
                    return NULL;
                }

                /* if block width is same as raster width, shortcut */
                if (nXBlocks == 1 && nYBlockSize > 1 && nXValid == width) {
                    x = 0;
                    y = nYBlockSize * iYBlock;

//                    RASTER_DEBUGF(4, "Setting set of pixel lines at (%d, %d) for %d pixels", x, y, nXValid * nYValid);
                    rt_band_set_pixel_line(band, x, y, values, nXValid * nYValid);
                }
                else {
                    ptr = values;
                    x = nXBlockSize * iXBlock;
                    for (iY = 0; iY < nYValid; iY++) {
                        y = iY + (nYBlockSize * iYBlock);

//                        RASTER_DEBUGF(4, "Setting pixel line at (%d, %d) for %d pixels", x, y, nXValid);
                        rt_band_set_pixel_line(band, x, y, ptr, nXValid);
                        ptr += (nXValid * ptlen);
                    }
                }
            }
        }

        /* free memory */
        rtdealloc(values);
    }

    return rast;
}

int
rt_band_is_offline(rt_band band) {

    assert(NULL != band);


    return band->offline ? 1 : 0;
}

//static void
//_rt_raster_geotransform_warn_offline_band(rt_raster raster) {
//    int numband = 0;
//    int i = 0;
//    rt_band band = NULL;
//
//    if (raster == NULL)
//        return;
//
//    numband = rt_raster_get_num_bands(raster);
//    if (numband < 1)
//        return;
//
//    for (i = 0; i < numband; i++) {
//        band = rt_raster_get_band(raster, i);
//        if (NULL == band)
//            continue;
//
//        if (!rt_band_is_offline(band))
//            continue;
//
//        rtwarn("Changes made to raster geotransform matrix may affect out-db band data. Returned band data may be incorrect");
//        break;
//    }
//}

