//
// Created by 何文婷 on 18/3/22.
//

#ifndef RASTERDB_LIBRTCORE_H
#define RASTERDB_LIBRTCORE_H
#include "gdal.h"
#include "stddef.h"
#include "stdint.h"
#include <float.h>
/* Pixel types */
typedef enum {
    PT_1BB=0,     /* 1-bit boolean            */
    PT_2BUI=1,    /* 2-bit unsigned integer   */
    PT_4BUI=2,    /* 4-bit unsigned integer   */
    PT_8BSI=3,    /* 8-bit signed integer     */
    PT_8BUI=4,    /* 8-bit unsigned integer   */
    PT_16BSI=5,   /* 16-bit signed integer    */
    PT_16BUI=6,   /* 16-bit unsigned integer  */
    PT_32BSI=7,   /* 32-bit signed integer    */
    PT_32BUI=8,   /* 32-bit unsigned integer  */
    PT_32BF=10,   /* 32-bit float             */
    PT_64BF=11,   /* 64-bit float             */
    PT_END=13
} rt_pixtype;

rt_pixtype rt_util_gdal_datatype_to_pixtype(GDALDataType gdt);
typedef struct rt_raster_t* rt_raster;
typedef struct rt_band_t* rt_band;

struct rt_raster_t {
    uint32_t size;
    uint16_t version;

    /* Number of bands, all share the same dimension
     * and georeference */
    uint16_t numBands;

    /* Georeference (in projection units) */
    double scaleX; /* pixel width */
    double scaleY; /* pixel height */
    double ipX; /* geo x ordinate of the corner of upper-left pixel */
    double ipY; /* geo y ordinate of the corner of bottom-right pixel */
    double skewX; /* skew about the X axis*/
    double skewY; /* skew about the Y axis */

    int32_t srid; /* spatial reference id */
    uint16_t width; /* pixel columns - max 65535 */
    uint16_t height; /* pixel rows - max 65535 */
    rt_band *bands; /* actual bands */

};

struct rt_extband_t {
    uint8_t bandNum; /* 0-based */
    char* path; /* internally owned */
    void *mem; /* loaded external band data, internally owned */
};

struct rt_band_t {
    rt_pixtype pixtype;
    int32_t offline;
    uint16_t width;
    uint16_t height;
    int32_t hasnodata; /* a flag indicating if this band contains nodata values */
    int32_t isnodata;   /* a flag indicating if this band is filled only with
                           nodata values. flag CANNOT be TRUE if hasnodata is FALSE */
    double nodataval; /* int will be converted ... */
    int8_t ownsdata; /* 0, externally owned. 1, internally owned. only applies to data.mem */

    rt_raster raster; /* reference to parent raster */

    union {
        void* mem; /* actual data, externally owned */
        struct rt_extband_t offline;
    } data;
};



rt_raster rt_raster_from_gdal_dataset(GDALDatasetH ds);
void rt_raster_set_srid(rt_raster raster, int32_t srid);
char *
rt_raster_to_hexwkb(rt_raster raster, int outasin, uint32_t *hexwkbsize);
void raster_destroy(rt_raster raster);

typedef enum {
    ES_NONE = 0, /* no error */
    ES_ERROR = 1 /* generic error */
} rt_errorstate;
rt_errorstate
rt_band_load_offline_data(rt_band band);
int
rt_pixtype_size(rt_pixtype pixtype);
int
rt_raster_get_num_bands(rt_raster raster);
rt_band
rt_raster_get_band(rt_raster raster, int n);
int
rt_band_is_offline(rt_band band);
void
rt_band_destroy(rt_band band);
char *
rt_raster_to_hexwkb(rt_raster raster, int outasin, uint32_t *hexwkbsize);
uint8_t *
rt_raster_to_wkb(rt_raster raster, int outasin, uint32_t *wkbsize);
uint8_t
isMachineLittleEndian(void);
uint16_t
rt_raster_get_width(rt_raster raster);
uint16_t
rt_raster_get_height(rt_raster raster);
#define FLT_NEQ(x, y) (fabs(x - y) > FLT_EPSILON)
#define FLT_EQ(x, y) (!FLT_NEQ(x, y))
uint8_t
rt_util_clamp_to_1BB(double value);

uint8_t
rt_util_clamp_to_2BUI(double value);

uint8_t
rt_util_clamp_to_4BUI(double value);

int8_t
rt_util_clamp_to_8BSI(double value);

uint8_t
rt_util_clamp_to_8BUI(double value);

int16_t
rt_util_clamp_to_16BSI(double value);

uint16_t
rt_util_clamp_to_16BUI(double value);

int32_t
rt_util_clamp_to_32BSI(double value);

uint32_t
rt_util_clamp_to_32BUI(double value);

float
rt_util_clamp_to_32F(double value);
rt_band
rt_band_new_inline(
        uint16_t width, uint16_t height,
        rt_pixtype pixtype,
        uint32_t hasnodata, double nodataval,
        uint8_t* data
);
void
rt_band_set_ownsdata_flag(rt_band band, int flag);
int
rt_raster_add_band(rt_raster raster, rt_band band, int index);
rt_errorstate
rt_band_set_isnodata_flag(rt_band band, int flag);
rt_errorstate
rt_band_set_pixel_line(
        rt_band band,
        int x, int y,
        void *vals, uint32_t len
);

void* rt_band_get_data(rt_band band);
void rt_raster_destroy(rt_raster raster);
rt_raster rt_raster_new(uint32_t width, uint32_t height);
int rt_raster_generate_new_band(
        rt_raster raster,
        rt_pixtype pixtype,
        double initialvalue,
        uint32_t hasnodata, double nodatavalue,
        int index
);
rt_errorstate
rt_util_gdal_sr_auth_info(GDALDatasetH hds, char **authname, char **authcode);
int rt_band_get_ownsdata_flag(rt_band band);

#endif //RASTERDB_LIBRTCORE_H
