#ifndef AG_CYPHER_NODE_H
#define AG_CYPHER_NODE_H

#include "postgres.h"

#include "nodes/extensible.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"

#include "nodes.h"

/*
 * clauses
 */

typedef struct cypher_return
{
    ExtensibleNode extensible;
    bool distinct;
    List *items;
    List *order_by;
    Node *skip;
    Node *limit;
} cypher_return;

typedef struct cypher_with
{
    ExtensibleNode extensible;
    bool distinct;
    List *items;
    List *order_by;
    Node *skip;
    Node *limit;
    Node *where;
} cypher_with;

typedef struct cypher_match
{
    ExtensibleNode extensible;
    List *pattern; // a list of cypher_paths
    Node *where; // optional WHERE subclause (expression)
} cypher_match;

typedef struct cypher_create
{
    ExtensibleNode extensible;
    List *pattern; // a list of cypher_paths
} cypher_create;

typedef struct cypher_set
{
    ExtensibleNode extensible;
    List *items; // a list of cypher_set_items
    bool is_remove; // true if this is REMOVE clause
} cypher_set;

typedef struct cypher_set_item
{
    ExtensibleNode extensible;
    Node *prop; // LHS
    Node *expr; // RHS
    bool is_add; // true if this is +=
} cypher_set_item;

typedef struct cypher_delete
{
    ExtensibleNode extensible;
    bool detach; // true if DETACH is specified
    List *exprs; // targets of this deletion
} cypher_delete;

/*
 * pattern
 */

typedef struct cypher_path
{
    ExtensibleNode extensible;
    List *path; // [ node ( , relationship , node , ... ) ]
} cypher_path;

// ( name :label props )
typedef struct cypher_node
{
    ExtensibleNode extensible;
    char *name;
    char *label;
    Node *props; // map or parameter
} cypher_node;

typedef enum
{
    CYPHER_REL_DIR_NONE,
    CYPHER_REL_DIR_LEFT,
    CYPHER_REL_DIR_RIGHT
} cypher_rel_dir;

// -[ name :label props ]-
typedef struct cypher_relationship
{
    ExtensibleNode extensible;
    char *name;
    char *label;
    Node *props; // map or parameter
    cypher_rel_dir dir;
} cypher_relationship;

/*
 * expression
 */

typedef struct cypher_bool_const
{
    ExtensibleNode extensible;
    bool boolean;
    int location;
} cypher_bool_const;

typedef struct cypher_map
{
    ExtensibleNode extensible;
    List *keyvals;
    int location;
} cypher_map;

typedef struct cypher_list
{
    ExtensibleNode extensible;
    List *elems;
    int location;
} cypher_list;

enum cypher_string_match_op
{
    CSMO_STARTS_WITH,
    CSMO_ENDS_WITH,
    CSMO_CONTAINS
};

typedef struct cypher_string_match
{
    ExtensibleNode extensible;
    enum cypher_string_match_op operation;
    Node *lhs;
    Node *rhs;
    int location;
} cypher_string_match;

/* clauses */
void out_cypher_return(StringInfo str, const ExtensibleNode *node);
void out_cypher_with(StringInfo str, const ExtensibleNode *node);
void out_cypher_match(StringInfo str, const ExtensibleNode *node);
void out_cypher_create(StringInfo str, const ExtensibleNode *node);
void out_cypher_set(StringInfo str, const ExtensibleNode *node);
void out_cypher_set_item(StringInfo str, const ExtensibleNode *node);
void out_cypher_delete(StringInfo str, const ExtensibleNode *node);

/* pattern */
void out_cypher_path(StringInfo str, const ExtensibleNode *node);
void out_cypher_node(StringInfo str, const ExtensibleNode *node);
void out_cypher_relationship(StringInfo str, const ExtensibleNode *node);

/* expression */
void out_cypher_bool_const(StringInfo str, const ExtensibleNode *node);
void out_cypher_map(StringInfo str, const ExtensibleNode *node);
void out_cypher_list(StringInfo str, const ExtensibleNode *node);

/* string match */
void out_cypher_string_match(StringInfo str, const ExtensibleNode *node);
#endif
