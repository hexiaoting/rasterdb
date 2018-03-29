#include <stdio.h>
#include <stdlib.h>
#include "loader/rasterdb_config.h"
#include "loader/load-raster.h"
#include "postgres.h"
#include "utils/builtins.h"

void usage();

PG_FUNCTION_INFO_V1(load_raster);

Datum
load_raster(PG_FUNCTION_ARGS) {
    const char *loadPath = text_to_cstring(PG_GETARG_TEXT_P(0));

    //S1: set config
    RTLOADERCFG *config = NULL;
    config = malloc(sizeof(RTLOADERCFG));
    if (config == NULL) {
        exit(1);
    }

    init_config(config);

    // path is directory or regular file
    struct stat s_buf;  
  
    /*获取文件信息，把信息放到s_buf中*/  
    stat(loadPath,&s_buf);  
  
    /*判断输入的文件路径是否目录，若是目录，则往下执行，分析目录下的文件*/  
    if(S_ISREG(s_buf.st_mode)) { 
        config->rt_file_count++;
        config->rt_file = (char **) rtrealloc(config->rt_file, sizeof(char *) * config->rt_file_count);
        if (config->rt_file == NULL) {
            rterror("Could not allocate memory for storing raster files");
            rtdealloc_config(config);
            exit(1);
        }

        config->rt_file[config->rt_file_count - 1] = rtalloc(sizeof(char) * (strlen(loadPath) + 1));
        if (config->rt_file[config->rt_file_count - 1] == NULL) {
            rterror("Could not allocate memory for storing raster filename");
            rtdealloc_config(config);
            exit(1);
        }
        strncpy(config->rt_file[config->rt_file_count - 1], loadPath, strlen(loadPath) + 1);
    } else if(S_ISREG(s_buf.st_mode)) {
       rterror("cannot process dir");
       rtdealloc_config(config);
       exit(1);
    } else {
       rterror("cannot process dir");
       rtdealloc_config(config);
       exit(1);
    }
    //S2: convert file format to geotiff

    //S3: analysis geotiff data to hexwkb
    if (config->rt_file_count != 0) {
        exit(1);
    }

    /****************************************************************************
     * validate raster files
     ****************************************************************************/
    analysis_raster(config);
    return 0;
}
