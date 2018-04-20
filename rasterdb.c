#include <stdio.h>
#include <stdlib.h>

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

typedef struct RasterdbFdwPlanState{
    char *location;
    List *options;
} RasterdbFdwPlanState;

typedef struct RasterdbFdwExecutionState {
    char *location;
    List *options;
    int next_tuple; /*index of next one to return*/
    int num_tuples; /* # of tuples in array*/
    bool eof_reached; /* true if last fetch reached EOF */
    HeapTuple *tuples; /*array of currently-retrieved tuples*/
    AttInMetadata *attinmeta;
    int cur_lineno;
    MemoryContext batch_context;
    MemoryContext temp_context;
} RasterdbFdwExecutionState;

/* Module load callback */
void _PG_init(void);
void _PG_fini(void);
void _PG_init(void) {
    const char* pszGDAL_SKIP = CPLGetConfigOption( "GDAL_SKIP", NULL );
    if( pszGDAL_SKIP != NULL ) {
        elog(DEBUG1, "rasterdb _PG_init(void)hwt: GDAL_SKIP set :%s", pszGDAL_SKIP);
    } else {
        elog(DEBUG1, "rasterdb _PG_init hwt: GDAL_SKIP not set");
    }
    CPLSetConfigOption( "GDAL_SKIP", "" );
	elog(INFO, "create extension rasterdb");
}
void _PG_fini(void) {
	elog(INFO, "drop extension rasterdb");
}

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
//static void rasterdbExplainForeignScan(ForeignScanState *node, ExplainState *es);
static void rasterdbBeginForeignScan(ForeignScanState *s_cunode, int eflags);
static TupleTableSlot *rasterdbIterateForeignScan(ForeignScanState *snode);
//static void rasterdbReScanForeignScan(ForeignScanState *node);
static void rasterdbEndForeignScan(ForeignScanState *node);
//static bool rasterdbAnalyzeForeignTable(Relation relation,
//	AcquireSampleRowsFunc *func,
//	BlockNumber *totalpages);
static void
rasterGetOption(Oid foreigntableid,
	char **location, List **other_options);
static HeapTuple
make_tuple_from_string(char *str, Relation rel, AttInMetadata *attinmeta,
        MemoryContext temp_context); 

static int
GetRasterBatch(char *location, List *options, int cur_lineno, int batchsize, char **buf);

Datum
rasterdb_fdw_handler(PG_FUNCTION_ARGS) {
    FdwRoutine *fdwroutine = makeNode(FdwRoutine);

    fdwroutine->GetForeignRelSize = rasterdbGetForeignRelSize;
    fdwroutine->GetForeignPaths = rasterdbGetForeignPaths;
    fdwroutine->GetForeignPlan = rasterdbGetForeignPlan;
//    fdwroutine->ExplainForeignScan = rasterdbExplainForeignScan;
    fdwroutine->BeginForeignScan = rasterdbBeginForeignScan;
    fdwroutine->IterateForeignScan = rasterdbIterateForeignScan;
//    fdwroutine->ReScanForeignScan = rasterdbReScanForeignScan;
    fdwroutine->EndForeignScan = rasterdbEndForeignScan;
//    fdwroutine->AnalyzeForeignTable = rasterdbAnalyzeForeignTable;

    PG_RETURN_POINTER(fdwroutine);
}

Datum
rasterdb_fdw_validator(PG_FUNCTION_ARGS)
{
	Datum res =BoolGetDatum(true);
	return res;
}

static void rasterdbBeginForeignScan(ForeignScanState *node, int eflags) {
    RasterdbFdwExecutionState *festate;
    char *location;
    List *options;
    EState *estate = node->ss.ps.state;

    if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
        return;

    festate = (RasterdbFdwExecutionState *)palloc(sizeof(RasterdbFdwExecutionState));
    node->fdw_state = (void *)festate;

    rasterGetOption(RelationGetRelid(node->ss.ss_currentRelation), 
            &location, &options);
    festate->location = location;
    festate->options = options;
    festate->cur_lineno = 0;
    festate->num_tuples = 0;
    festate->next_tuple = 0;
    festate->eof_reached = false;
    festate->tuples = NULL;
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
}

static void
fetch_more_data(ForeignScanState *node) {
    RasterdbFdwExecutionState *festate = (RasterdbFdwExecutionState *) node->fdw_state;
    int batchsize = 100;
    int numrows = 0; // fetched rasterdb rows
    int i = 0;
    char **buf = NULL;
    MemoryContext oldcontext;

    festate->tuples = NULL;

    MemoryContextReset(festate->batch_context);
    oldcontext = MemoryContextSwitchTo(festate->batch_context);

    buf = palloc0(sizeof(char *) * batchsize);

    numrows = GetRasterBatch(festate->location, 
                            festate->options, 
                            festate->cur_lineno,
                            batchsize,
                            buf);

    festate->tuples = (HeapTuple *)palloc0 (numrows * sizeof(HeapTuple));
    for (i = 0; i < numrows; i++) {
        festate->tuples[i] = 
            make_tuple_from_string(buf[i], node->ss.ss_currentRelation, 
                    festate->attinmeta,
                    festate->temp_context);
        //elog(DEBUG1, "new tuple[%d]->data=%x",i, &(festate->tuples[i]->t_data));
        //TODO: memory leak????
        //pfree(buf[i]);
    }
    
    pfree(buf);

    festate->next_tuple = 0;
    festate->num_tuples = numrows;
    festate->eof_reached = (numrows < batchsize);

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

static TupleTableSlot *rasterdbIterateForeignScan(ForeignScanState *node) {
    RasterdbFdwExecutionState *festate = (RasterdbFdwExecutionState *) node->fdw_state;
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
    const char* pszGDAL_SKIP = CPLGetConfigOption( "GDAL_SKIP", NULL );
    if( pszGDAL_SKIP != NULL ) {
        elog(DEBUG1, "hwt----IterateForeignScan: GDAL_SKIP set='%s'", pszGDAL_SKIP);
    } else {
        elog(DEBUG1, "hwt----IteraeForeignScan: GDAL_SKIP not set");
    }

    if (festate->next_tuple >= festate->num_tuples)
    {
        if (!festate->eof_reached)
            fetch_more_data(node);
        if (festate->next_tuple >= festate->num_tuples)
            return ExecClearTuple(slot);
    }

    //elog(DEBUG1, "Iterate return tuple->data=%x", (void *)(festate->tuples[festate->next_tuple]->t_data));
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
 * Set baserel->fdw_private
 */
static void rasterdbGetForeignRelSize(PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid) {
    RasterdbFdwPlanState *fdw_private;

    fdw_private = (RasterdbFdwPlanState *)palloc0(sizeof(RasterdbFdwPlanState));
    rasterGetOption(foreigntableid, &fdw_private->location, &fdw_private->options);

    baserel->fdw_private = (void *)fdw_private;

    baserel->rows += 1;

}


static void
estimate_costs(PlannerInfo *root, RelOptInfo *baserel,
                           RasterdbFdwPlanState *fdw_private,
                           Cost *startup_cost, Cost *total_cost) {
        //BlockNumber pages = fdw_private->pages;
        //double          ntuples = fdw_private->ntuples;
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
    RasterdbFdwPlanState *fdw_private = (RasterdbFdwPlanState *) baserel->fdw_private;
    ForeignPath *path;
    Cost startup_cost;
    Cost total_cost;

    /* Estimate costs */
    estimate_costs(root, baserel, fdw_private,
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
            best_path->fdw_private,
            NIL,
            NIL,
            outer_plan);
}

static void
rasterGetOption(Oid foreigntableid,
	char **location, List **other_options)
{
    List       *options = NIL;
    ListCell   *lc, *prev;
    ForeignTable *table;

    table = GetForeignTable(foreigntableid);
    options = list_concat(options, table->options);

    *location = NULL;
    prev = NULL;
    foreach(lc, options) {
	DefElem *def = (DefElem *)lfirst(lc);
	if (!strcmp(def->defname, "location")) {
	    *location = defGetString(def);
	    options = list_delete_cell(options, lc, prev);
	    break;
	}
	prev = lc;
    }

    if (*location == NULL)
	    elog(ERROR, "location is required for rasterdb_fdw foreign tables");

    *other_options = options;
}




PG_FUNCTION_INFO_V1(load_raster);

Datum
load_raster(PG_FUNCTION_ARGS) {
	Datum res = BoolGetDatum(true);
    const char *loadPath = text_to_cstring(PG_GETARG_TEXT_P(0));
    // path is directory or regular file
    struct stat s_buf;
    RTLOADERCFG *config = NULL;

    //S1: set config
    config = rtalloc(sizeof(RTLOADERCFG));
    if (config == NULL) {
        exit(1);
    }

    init_config(config);
    set_config(config);

    // path is directory or regular file
    elog(INFO, "<---process config");
  
    stat(loadPath,&s_buf);  
  
    /*
     * rasterdb-v1: location is a filename in this version
     * TODO: process dir
     */
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
    } else if(S_ISDIR(s_buf.st_mode)) {
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
    if (config->rt_file_count == 0) {
       rtdealloc_config(config);
       exit(1);
    }

    /****************************************************************************
     * validate raster files
     ****************************************************************************/
    //analysis_raster(config);
    return res;
}

static int
GetRasterBatch(char *location, List *options, int cur_lineno, int batchsize, char **buf) {
	ListCell *option;
    RTLOADERCFG *config = NULL;
    
    // path is directory or regular file
    struct stat s_buf;

    //S1: set config
    config = rtalloc(sizeof(RTLOADERCFG));
    if (config == NULL) {
        exit(1);
    }
    init_config(config);
    
    // path is directory or regular file
    stat(location, &s_buf);  
  
    /*
     * rasterdb-v1: location is a filename in this version
     * TODO: process dir
     */
    if(S_ISREG(s_buf.st_mode)) { 
        config->rt_file_count++;
        config->rt_file = (char **) rtrealloc(config->rt_file, sizeof(char *) * config->rt_file_count);
        if (config->rt_file == NULL) {
            rterror("Could not allocate memory for storing raster files");
            rtdealloc_config(config);
            exit(1);
        }

        config->rt_file[config->rt_file_count - 1] = rtalloc(sizeof(char) * (strlen(location) + 1));
        if (config->rt_file[config->rt_file_count - 1] == NULL) {
            rterror("Could not allocate memory for storing raster filename");
            rtdealloc_config(config);
            exit(1);
        }
        strncpy(config->rt_file[config->rt_file_count - 1], location, strlen(location) + 1);
    } else if(S_ISDIR(s_buf.st_mode)) {
        elog(ERROR, "cannot process location dir in v1");
        rtdealloc_config(config);
    } else {
        elog(ERROR, "cannot process this location  in v1");
        rtdealloc_config(config);
    }
    if (config->rt_file_count == 0) {
       rtdealloc_config(config);
       elog(INFO, "this location contain no available data ");
       return 0;
    }

    foreach(option, options) {
        DefElem *defel = (DefElem *)lfirst(option);
        if (strcmp(defel->defname, "tile_size") == 0) {
            char *tile = defGetString(defel);
            char *p = strchr(tile, 'x');
            if (p != NULL) {
                char s1[5] = {0};
                strncpy(s1, tile, p - tile);
                config->tile_size[0] = atoi(s1);
                config->tile_size[1] = atoi(p + 1);
                elog(DEBUG1, "config->tile_size=%dx%d",config->tile_size[0],config->tile_size[1]);
            }
        }
        else {
            elog(INFO, "Do not recognise option %s", ((DefElem *)lfirst(option))->defname);
        }
    }
    //S3: analysis geotiff data to hexwkb
    return analysis_raster(config, cur_lineno, batchsize, buf);
}
