#include "postgres.h"

#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "nodes/relation.h"

#include "executor/cypher_executor.h"
#include "optimizer/cypher_createplan.h"

const CustomScanMethods cypher_create_plan_methods = {
    "Cypher Create", create_cypher_create_plan_state};

Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                              CustomPath *best_path, List *tlist,
                              List *clauses, List *custom_plans)
{
    CustomScan *cs;

    cs = makeNode(CustomScan);

    cs->scan.plan.startup_cost = best_path->path.startup_cost;
    cs->scan.plan.total_cost = best_path->path.total_cost;

    cs->scan.plan.plan_rows = best_path->path.rows;
    cs->scan.plan.plan_width = 0;

    cs->scan.plan.parallel_aware = best_path->path.parallel_aware;
    cs->scan.plan.parallel_safe = best_path->path.parallel_safe;

    cs->scan.plan.plan_node_id = 0; // Set later in set_plan_refs
    cs->scan.plan.targetlist = tlist;
    cs->scan.plan.qual = NIL; // XXX: clauses?
    cs->scan.plan.lefttree = NULL;
    cs->scan.plan.righttree = NULL;
    cs->scan.plan.initPlan = NIL;

    cs->scan.plan.extParam = NULL;
    cs->scan.plan.allParam = NULL;

    cs->scan.scanrelid = 0;

    cs->flags = best_path->flags;

    cs->custom_plans = custom_plans;
    cs->custom_exprs = NIL;
    cs->custom_private = best_path->custom_private;
    cs->custom_scan_tlist = tlist; // XXX: optional?
    cs->custom_relids = NULL;
    cs->methods = &cypher_create_plan_methods;

    return (Plan *)cs;
}
