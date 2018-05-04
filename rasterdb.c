#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "rasterdb_config.h"
#include "load_raster.h"
#include "postgres.h"
#include "utils/builtins.h"
#include "gdal.h"
#include "cpl_conv.h"
#include "access/htup_details.h"
#include "commands/defrem.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "optimizer/cost.h"
#include "optimizer/planmain.h"
#include "foreign/fdwapi.h"
#include "optimizer/restrictinfo.h"
#include "executor/executor.h"
#include "funcapi.h"
#include "utils/rel.h"
#include "utils/memutils.h"
#include "commands/defrem.h"
#include "optimizer/pathnode.h"
#include "access/htup_details.h"
#include "nodes/pg_list.h"
#include "utils/memutils.h"

#define FILENAME_MAXSIZE 50
typedef struct RasterdbFdwPlanState{
    char *location;
    List *options;
} RasterdbFdwPlanState;

typedef struct RasterdbFdwExecutionState {
    int rt_file_count; /*total files# */
    int cur_fileno; /*Current Processing file# */
    int cur_lineno;
    int next_tuple; /*index of next one tuple to return*/
    int num_tuples; /* # of tuples in array*/
    bool eof_curfile_reached; /* true if last raw fetched in current file*/
    char *conf_file;
    char **rt_files; /*filenames[], size==rt_file_count*/
    List *options;
    RTLOADERCFG *config;
    HeapTuple *tuples; /*array of currently-retrieved tuples*/
    AttInMetadata *attinmeta;
    MemoryContext batch_context;
    MemoryContext temp_context;
} RasterdbFdwExecutionState;

/* Module load callback */
void _PG_init(void);
void _PG_fini(void);

PG_FUNCTION_INFO_V1(rasterdb_fdw_handler);
PG_FUNCTION_INFO_V1(rasterdb_fdw_validator);

static void rasterdbGetForeignRelSize(PlannerInfo *root,
	RelOptInfo *dbaserel,
	Oid foreigntableid);
static void rasterdbGetForeignPaths(PlannerInfo *root,
	RelOptInfo *elbaserel,
	Oid foreigntableid);
static ForeignScan *rasterdbGetForeignPlan(PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid,
	ForeignPath *best_path,
	List *tlist,
	List *scan_clauses,
	Plan *outer_plan);
static void rasterdbBeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *rasterdbIterateForeignScan(ForeignScanState *snode);
static void rasterdbEndForeignScan(ForeignScanState *node);

static void rasterGetOption(Oid foreigntableid,
	char **conf_file, List **other_options);
static HeapTuple make_tuple_from_string(char *str, Relation rel, AttInMetadata *attinmeta,
        MemoryContext temp_context); 
static void fetch_more_data(ForeignScanState *node, bool nextfile); 

void _PG_init(void) {
	elog(INFO, "create extension rasterdb");

    /*
     * Set env POSTGIS_GDAL_ENABLED_DRIVERS=ENBALE_ALL 
     * as the bootvalue in rtpostgis __PG_init()
     */
    if(putenv("POSTGIS_GDAL_ENABLED_DRIVERS=ENABLE_ALL"))
        elog(ERROR, "putenv failed.");
}

void _PG_fini(void) {
	elog(INFO, "drop extension rasterdb");
}

Datum rasterdb_fdw_handler(PG_FUNCTION_ARGS) {
    FdwRoutine *fdwroutine = makeNode(FdwRoutine);
    fdwroutine->GetForeignRelSize = rasterdbGetForeignRelSize;
    fdwroutine->GetForeignPaths = rasterdbGetForeignPaths;
    fdwroutine->GetForeignPlan = rasterdbGetForeignPlan;
    fdwroutine->BeginForeignScan = rasterdbBeginForeignScan;
    fdwroutine->IterateForeignScan = rasterdbIterateForeignScan;
    fdwroutine->EndForeignScan = rasterdbEndForeignScan;
//    fdwroutine->ExplainForeignScan = rasterdbExplainForeignScan;
//    fdwroutine->ReScanForeignScan = rasterdbReScanForeignScan;
//    fdwroutine->AnalyzeForeignTable = rasterdbAnalyzeForeignTable;

    PG_RETURN_POINTER(fdwroutine);
}

Datum rasterdb_fdw_validator(PG_FUNCTION_ARGS)
{
	Datum res =BoolGetDatum(true);
	return res;
}

static void rasterdbBeginForeignScan(ForeignScanState *node, int eflags) {
    RasterdbFdwExecutionState *festate;
    RTLOADERCFG *config;
    struct stat s_buf;
    char *conf_file;
    List *options;
    char *location;
    EState *estate = node->ss.ps.state;

    if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
        return;

    festate = (RasterdbFdwExecutionState *)palloc0(sizeof(RasterdbFdwExecutionState));
    node->fdw_state = (void *)festate;

    /*
     * Set variables for festate
     */
    rasterGetOption(RelationGetRelid(node->ss.ss_currentRelation), 
            &conf_file, &options);
    festate->conf_file = conf_file;
    festate->options = options;
    festate->cur_lineno = 0;
    festate->num_tuples = 0;
    festate->next_tuple = 0;
    festate->eof_curfile_reached = false;
    festate->cur_fileno = 0;
    festate->tuples = NULL;
    festate->rt_file_count = 0;
    festate->rt_files = NULL;
    festate->attinmeta = TupleDescGetAttInMetadata(RelationGetDescr(node->ss.ss_currentRelation));
    festate->batch_context = AllocSetContextCreate(estate->es_query_cxt,
            "rasterdb_fdw temporary data",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    festate->temp_context = AllocSetContextCreate(estate->es_query_cxt,
            "rasterdb_fdw temporary data",
            ALLOCSET_SMALL_MINSIZE,
            ALLOCSET_SMALL_INITSIZE,
            ALLOCSET_SMALL_MAXSIZE);

    //Set raster files
    set_config(&(festate->config), festate->conf_file);
    config = festate->config;
    location = config->location;
    stat(location, &s_buf);  

    GDALAllRegister();
    //check that GDAL recognizes all files 
    if(S_ISREG(s_buf.st_mode)) { 
        if (GDALIdentifyDriver(location, NULL) == NULL) {
            elog(INFO, "Unable to read raster file: %s", location);
        }
        festate->rt_files = (char **) rtrealloc(festate->rt_files, sizeof(char *) * (1 + festate->rt_file_count));
        if (festate->rt_files == NULL) {
            rtdealloc_config(config);
            elog(ERROR, "Could not allocate memory for storing raster files");
        }

        festate->rt_files[festate->rt_file_count] = rtalloc(sizeof(char) * (strlen(location) + 1));
        if (festate->rt_files[festate->rt_file_count] == NULL) {
            rtdealloc_config(config);
            elog(ERROR, "Could not allocate memory for storing raster filename");
        }
        strcpy(festate->rt_files[festate->rt_file_count], location);
        festate->rt_file_count++;
    } else if (S_ISDIR(s_buf.st_mode)) {
        int tmp_length= 0;
        char filename[FILENAME_MAXSIZE];
        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(location)) != NULL) {
            // print all the files and directories within directory 
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
                    elog(DEBUG1, "Do not support %s type %d", entry->d_name, entry->d_type);
                    continue;
                }
                tmp_length = strlen(location) + strlen(entry->d_name) + 2;
                memset(filename, 0, 50);
                snprintf(filename,
                        tmp_length,
                        "%s/%s",
                        location,
                        entry->d_name);
                if (GDALIdentifyDriver(filename, NULL) == NULL) {
                    elog(INFO, "GDAL identify raster failed:%s", filename);
                    continue;
                }
                festate->rt_files = (char **) rtrealloc(festate->rt_files, sizeof(char *) * (1 + festate->rt_file_count));
                if (festate->rt_files == NULL) {
                    rtdealloc_config(config);
                    elog(ERROR, "Could not allocate memory for storing raster files");
                }

                festate->rt_files[festate->rt_file_count] = rtalloc(sizeof(char) * (tmp_length + 1));
                if (festate->rt_files[festate->rt_file_count] == NULL) {
                    rtdealloc_config(config);
                    elog(ERROR, "Could not allocate memory for storing raster filename");
                }
                strcpy(festate->rt_files[festate->rt_file_count], 
                        filename);
                festate->rt_file_count++;
                //elog(INFO, "add file %s", filename);
            }
            if(closedir(dir) == -1) {
                elog(ERROR, "closedir failed. errno=%d ", errno);
            }
        } else {
            elog(ERROR, "Cannot open dir:%s", location);
        }
    } else {
        elog(ERROR, "Location(%s) is not a file or directory !", location);
    }

    /*no file find in config location*/
    if (festate->rt_file_count == 0) {
        elog(INFO, "No file added to festate->rt_files");
    }
}


static TupleTableSlot *rasterdbIterateForeignScan(ForeignScanState *node) {
    RasterdbFdwExecutionState *festate = (RasterdbFdwExecutionState *) node->fdw_state;
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;

    if(festate->rt_file_count == 0)
        return ExecClearTuple(slot);

    /*
     * S1: Read tuple from buffer(festate->tuples), 
     *      if Current festate->tuples buffer is consumed. Then goto S2
     * S2: Get more data from current file
     *      if Get Some ,then read from current festate->tuples
     *      else: goto S3
     * S3: Get data from next file
     *      if Get some, then read from festate->tuples
     *      else(all files data is alrady iterated): goto S4
     * S4: return null
     */
    if (festate->next_tuple >= festate->num_tuples)
    {
        festate->num_tuples = 0;
        // Read current file
        if (!festate->eof_curfile_reached && festate->rt_file_count) {
            //elog(INFO, "read current file %s", festate->rt_files[festate->cur_fileno]);
            fetch_more_data(node, false);
        }

        // If current file is already eof, then Read next file
        if (festate->num_tuples == 0 && festate->eof_curfile_reached) {
            if (festate->cur_fileno < festate->rt_file_count - 1) {
                //elog(INFO, "current file eof, read another %s", festate->rt_files[festate->cur_fileno + 1]);
                fetch_more_data(node, true);
            } else {
                return ExecClearTuple(slot);
            }
        } 
    }

    ExecStoreTuple(festate->tuples[festate->next_tuple++],
            slot,
            InvalidBuffer,
            false);

    return slot;
}


static void rasterdbEndForeignScan(ForeignScanState *node) {
    RasterdbFdwExecutionState *festate = (RasterdbFdwExecutionState *) node->fdw_state;
    if (festate == NULL)
        return;
}

/*
 * TODO
 * Set baserel->fdw_private
 */
static void rasterdbGetForeignRelSize(PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid) {

    baserel->rows += 1;

}

static void
estimate_costs(PlannerInfo *root, RelOptInfo *baserel,
                           Cost *startup_cost, Cost *total_cost) {
        BlockNumber pages = 1;
        double          ntuples = 1;
        Cost            run_cost = 0;
        Cost            cpu_per_tuple;

        /*
         * We estimate costs almost the same way as cost_seqscan(), thus assuming
         * that I/O costs are equivalent to a regular table file of the same size.
         * However, we take per-tuple CPU costs as 10x of a seqscan, to account
         * for the cost of parsing records.
         */
        run_cost += seq_page_cost * pages;

        *startup_cost = baserel->baserestrictcost.startup;
        cpu_per_tuple = cpu_tuple_cost * 10 + baserel->baserestrictcost.per_tuple;
        run_cost += cpu_per_tuple * ntuples;
        *total_cost = *startup_cost + run_cost;
}

static void rasterdbGetForeignPaths(PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid) {
    //RasterdbFdwPlanState *fdw_private = (RasterdbFdwPlanState *) baserel->fdw_private;
    ForeignPath *path;
    Cost startup_cost;
    Cost total_cost;

    /* Estimate costs */
    estimate_costs(root, baserel,
                    &startup_cost, &total_cost);

    path = create_foreignscan_path(root, baserel,
                                    NULL,
                                    baserel->rows,
                                    startup_cost,
                                    total_cost,
                                    NIL,
                                    NULL,
                                    NULL,
                                    NIL);
    add_path(baserel, (Path *)path);
}

/*
 * Create a foreign scan plan node for  scanning the foreign tables
 */
static ForeignScan *rasterdbGetForeignPlan(PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid,
	ForeignPath *best_path,
	List *tlist,
	List *scan_clauses,
	Plan *outer_plan) {

    Index scan_relid = baserel->relid;
    
    //TODO: I donot understand what this sentence do .
    scan_clauses = extract_actual_clauses(scan_clauses, false);

    return make_foreignscan(tlist,
            scan_clauses,
            scan_relid,
            NIL,
            NULL,
           // best_path->fdw_private,
            NIL,
            NIL,
            outer_plan);
}

static void
rasterGetOption(Oid foreigntableid,
	char **conf_file, List **other_options)
{
    List       *options = NIL;
    ListCell   *lc, *prev;
    ForeignTable *table;

    table = GetForeignTable(foreigntableid);
    options = list_concat(options, table->options);

    /* extract conf_file option from create table sentence */
    *conf_file = NULL;
    prev = NULL;
    foreach(lc, options) {
        DefElem *def = (DefElem *)lfirst(lc);
        if (!strcmp(def->defname, "conf_file")) {
            *conf_file = defGetString(def);
            options = list_delete_cell(options, lc, prev);
            break;
        }
        prev = lc;
    }

    if (*conf_file == NULL)
	    elog(ERROR, "conf_file is required for rasterdb_fdw foreign tables");

    *other_options = options;
}

static void
fetch_more_data(ForeignScanState *node, bool nextfile) {
    RasterdbFdwExecutionState *festate = (RasterdbFdwExecutionState *) node->fdw_state;
    AttInMetadata *attinmeta = festate->attinmeta;
    int batchsize = festate->config->batchsize;
    int numrows = 0; // fetched rasterdb rows
    int i = 0;
    char **buf = NULL;
    MemoryContext oldcontext;
    char *filename;

    festate->tuples = NULL;

    MemoryContextReset(festate->batch_context);
    oldcontext = MemoryContextSwitchTo(festate->batch_context);

    buf = palloc0(sizeof(char *) * batchsize);
    if(nextfile) {
        festate->cur_lineno = 0;
        festate->cur_fileno++;
    }
    filename = festate->rt_files[festate->cur_fileno];
    elog(INFO, "Processing file:%s", filename);

    numrows = analysis_raster(filename, festate->config, festate->cur_lineno, buf);

    festate->tuples = (HeapTuple *)palloc0(numrows * sizeof(HeapTuple));
    for (i = 0; i < numrows; i++) {
        festate->tuples[i] = 
            make_tuple_from_string(buf[i], node->ss.ss_currentRelation, 
                    attinmeta,
                    festate->temp_context);
    }
    
    pfree(buf);

    festate->cur_lineno += numrows;
    festate->next_tuple = 0;
    festate->num_tuples = numrows;
    festate->eof_curfile_reached = (numrows < batchsize);

    MemoryContextSwitchTo(oldcontext);
}

static HeapTuple
make_tuple_from_string(char *str, Relation rel, AttInMetadata *attinmeta,
        MemoryContext temp_context) {
    HeapTuple tuple;
    TupleDesc tupledesc = RelationGetDescr(rel);
    Datum *values;
    bool *nulls;
    MemoryContext oldcontext;

    Assert(tupledesc->nattrs = 1);

    oldcontext = MemoryContextSwitchTo(temp_context);

    values = (Datum *) palloc0(tupledesc->natts * sizeof(Datum));
    nulls = (bool *) palloc0(tupledesc->natts * sizeof(bool));
    memset(nulls, true, tupledesc->natts * sizeof(bool));
    nulls[0] = (str == NULL);
    values[0] = InputFunctionCall(&attinmeta->attinfuncs[0],
            str,
            attinmeta->attioparams[0],
            attinmeta->atttypmods[0]
            );

    MemoryContextSwitchTo(oldcontext);
    tuple = heap_form_tuple(tupledesc, values, nulls);
    MemoryContextReset(temp_context);

    return tuple;
}
