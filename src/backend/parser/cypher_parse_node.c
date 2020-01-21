#include "postgres.h"

#include "mb/pg_wchar.h"
#include "nodes/primnodes.h"
#include "parser/parse_node.h"

#include "parser/cypher_parse_node.h"

static void errpos_ecb(void *arg);

// NOTE: sync the logic with make_parsestate()
cypher_parsestate *make_cypher_parsestate(cypher_parsestate *parent_cpstate)
{
    ParseState *parent_pstate = (ParseState *)parent_cpstate;
    cypher_parsestate *cpstate;
    ParseState *pstate;

    cpstate = palloc0(sizeof(*cpstate));

    pstate = (ParseState *)cpstate;
    pstate->parentParseState = parent_pstate;
    pstate->p_next_resno = 1;
    pstate->p_resolve_unknowns = true;

    if (parent_cpstate)
    {
        pstate->p_sourcetext = parent_pstate->p_sourcetext;
        pstate->p_queryEnv = parent_pstate->p_queryEnv;
        pstate->p_pre_columnref_hook = parent_pstate->p_pre_columnref_hook;
        pstate->p_post_columnref_hook = parent_pstate->p_post_columnref_hook;
        pstate->p_paramref_hook = parent_pstate->p_paramref_hook;
        pstate->p_coerce_param_hook = parent_pstate->p_coerce_param_hook;
        pstate->p_ref_hook_state = parent_pstate->p_ref_hook_state;

        cpstate->graph_name = parent_cpstate->graph_name;
        cpstate->params = parent_cpstate->params;
    }

    return cpstate;
}

void free_cypher_parsestate(cypher_parsestate *cpstate)
{
    free_parsestate((ParseState *)cpstate);
}

void setup_errpos_ecb(errpos_ecb_state *ecb_state, ParseState *pstate,
                      int query_loc)
{
    ecb_state->ecb.previous = error_context_stack;
    ecb_state->ecb.callback = errpos_ecb;
    ecb_state->ecb.arg = ecb_state;
    ecb_state->pstate = pstate;
    ecb_state->query_loc = query_loc;

    error_context_stack = &ecb_state->ecb;
}

void cancel_errpos_ecb(errpos_ecb_state *ecb_state)
{
    error_context_stack = ecb_state->ecb.previous;
}

/*
 * adjust the current error position by adding the position of the current
 * query which is a subquery of a parent query
 */
static void errpos_ecb(void *arg)
{
    errpos_ecb_state *ecb_state = arg;
    int query_pos;

    if (geterrcode() == ERRCODE_QUERY_CANCELED)
        return;

    Assert(ecb_state->query_loc > -1);
    query_pos = pg_mbstrlen_with_len(ecb_state->pstate->p_sourcetext,
                                     ecb_state->query_loc);
    errposition(query_pos + geterrposition());
}

