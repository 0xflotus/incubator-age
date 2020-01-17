#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"

#include "nodes/ag_nodes.h"
#include "optimizer/cypher_paths.h"
#include "parser/cypher_analyze.h"

PG_MODULE_MAGIC;

void _PG_init(void);

void _PG_init(void)
{
    register_ag_nodes();
    set_rel_pathlist_init();
    post_parse_analyze_init();
}

void _PG_fini(void);

void _PG_fini(void)
{
    post_parse_analyze_fini();
    set_rel_pathlist_fini();
}

PG_FUNCTION_INFO_V1(cypher);

Datum cypher(PG_FUNCTION_ARGS)
{
    const char *s;

    s = PG_ARGISNULL(0) ? "NULL" : PG_GETARG_CSTRING(0);

    ereport(ERROR, (errmsg_internal("unhandled cypher(cstring) function call"),
                    errdetail_internal("%s", s)));

    PG_RETURN_NULL();
}
