/*
 * I/O routines for agtype type
 *
 * Portions Copyright (c) 2014-2018, PostgreSQL Global Development Group
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/typcache.h"

#include "agtype.h"
#include "agtype_parser.h"

typedef struct agtype_in_state
{
    agtype_parse_state *parse_state;
    agtype_value *res;
} agtype_in_state;

typedef enum /* type categories for datum_to_agtype */
{
    AGT_TYPE_NULL, /* null, so we didn't bother to identify */
    AGT_TYPE_BOOL, /* boolean (built-in types only) */
    AGT_TYPE_INTEGER, /* Cypher Integer type */
    AGT_TYPE_FLOAT, /* Cypher Float type */
    AGT_TYPE_NUMERIC, /* numeric (ditto) */
    AGT_TYPE_DATE, /* we use special formatting for datetimes */
    AGT_TYPE_TIMESTAMP, /* we use special formatting for timestamp */
    AGT_TYPE_TIMESTAMPTZ, /* ... and timestamptz */
    AGT_TYPE_AGTYPE, /*AGTYPE */
    AGT_TYPE_JSON, /* JSON */
    AGT_TYPE_JSONB, /* JSONB */
    AGT_TYPE_ARRAY, /* array */
    AGT_TYPE_COMPOSITE, /* composite */
    AGT_TYPE_JSONCAST, /* something with an explicit cast to JSON */
    AGT_TYPE_OTHER /* all else */
} agt_type_category;

static inline Datum agtype_from_cstring(char *str, int len);
static size_t check_string_length(size_t len);
static void agtype_in_object_start(void *pstate);
static void agtype_in_object_end(void *pstate);
static void agtype_in_array_start(void *pstate);
static void agtype_in_array_end(void *pstate);
static void agtype_in_object_field_start(void *pstate, char *fname,
                                         bool isnull);
static void agtype_put_escaped_value(StringInfo out, agtype_value *scalar_val);
static void escape_agtype(StringInfo buf, const char *str);
static bool is_decimal_needed(char *numstr);
static void agtype_in_scalar(void *pstate, char *token,
                             agtype_token_type tokentype);
static void agtype_categorize_type(Oid typoid, agt_type_category *tcategory,
                                   Oid *outfuncoid);
static void composite_to_agtype(Datum composite, agtype_in_state *result);
static void array_dim_to_agtype(agtype_in_state *result, int dim, int ndims,
                                int *dims, Datum *vals, bool *nulls,
                                int *valcount, agt_type_category tcategory,
                                Oid outfuncoid);
static void array_to_agtype_internal(Datum array, agtype_in_state *result);
static void datum_to_agtype(Datum val, bool is_null, agtype_in_state *result,
                            agt_type_category tcategory, Oid outfuncoid,
                            bool key_scalar);
static void add_agtype(Datum val, bool is_null, agtype_in_state *result,
                       Oid val_type, bool key_scalar);
static char *agtype_to_cstring_worker(StringInfo out, agtype_container *in,
                                      int estimated_len, bool indent);
static void add_indent(StringInfo out, bool indent, int level);

PG_FUNCTION_INFO_V1(agtype_in);

/*
 * agtype type input function
 */
Datum agtype_in(PG_FUNCTION_ARGS)
{
    char *str = PG_GETARG_CSTRING(0);

    return agtype_from_cstring(str, strlen(str));
}

PG_FUNCTION_INFO_V1(agtype_out);

/*
 * agtype type output function
 */
Datum agtype_out(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    char *out;

    out = agtype_to_cstring(NULL, &agt->root, VARSIZE(agt));

    PG_RETURN_CSTRING(out);
}

/*
 * agtype_from_cstring
 *
 * Turns agtype string into an agtype Datum.
 *
 * Uses the agtype parser (with hooks) to construct an agtype.
 */
static inline Datum agtype_from_cstring(char *str, int len)
{
    agtype_lex_context *lex;
    agtype_in_state state;
    agtype_sem_action sem;

    memset(&state, 0, sizeof(state));
    memset(&sem, 0, sizeof(sem));
    lex = make_agtype_lex_context_cstring_len(str, len, true);

    sem.semstate = (void *)&state;

    sem.object_start = agtype_in_object_start;
    sem.array_start = agtype_in_array_start;
    sem.object_end = agtype_in_object_end;
    sem.array_end = agtype_in_array_end;
    sem.scalar = agtype_in_scalar;
    sem.object_field_start = agtype_in_object_field_start;

    parse_agtype(lex, &sem);

    /* after parsing, the item member has the composed agtype structure */
    PG_RETURN_POINTER(agtype_value_to_agtype(state.res));
}

static size_t check_string_length(size_t len)
{
    if (len > AGTENTRY_OFFLENMASK)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg("string too long to represent as agtype string"),
             errdetail(
                 "Due to an implementation restriction, agtype strings cannot exceed %d bytes.",
                 AGTENTRY_OFFLENMASK)));
    }

    return len;
}

static void agtype_in_object_start(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_OBJECT,
                                    NULL);
}

static void agtype_in_object_end(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_END_OBJECT,
                                    NULL);
}

static void agtype_in_array_start(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_ARRAY,
                                    NULL);
}

static void agtype_in_array_end(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_END_ARRAY,
                                    NULL);
}

static void agtype_in_object_field_start(void *pstate, char *fname,
                                         bool isnull)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;
    agtype_value v;

    Assert(fname != NULL);
    v.type = AGTV_STRING;
    v.val.string.len = check_string_length(strlen(fname));
    v.val.string.val = fname;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_KEY, &v);
}

static void agtype_put_escaped_value(StringInfo out, agtype_value *scalar_val)
{
    char *numstr;

    switch (scalar_val->type)
    {
    case AGTV_NULL:
        appendBinaryStringInfo(out, "null", 4);
        break;
    case AGTV_STRING:
        escape_agtype(out, pnstrdup(scalar_val->val.string.val,
                                    scalar_val->val.string.len));
        break;
    case AGTV_NUMERIC:
        appendStringInfoString(
            out, DatumGetCString(DirectFunctionCall1(
                     numeric_out, PointerGetDatum(scalar_val->val.numeric))));
        break;
    case AGTV_INTEGER:
        appendStringInfoString(
            out, DatumGetCString(DirectFunctionCall1(
                     int8out, Int64GetDatum(scalar_val->val.int_value))));
        break;
    case AGTV_FLOAT:
        numstr = DatumGetCString(DirectFunctionCall1(
            float8out, Float8GetDatum(scalar_val->val.float_value)));
        appendStringInfoString(out, numstr);

        if (is_decimal_needed(numstr))
            appendBinaryStringInfo(out, ".0", 2);
        break;
    case AGTV_BOOL:
        if (scalar_val->val.boolean)
            appendBinaryStringInfo(out, "true", 4);
        else
            appendBinaryStringInfo(out, "false", 5);
        break;
    default:
        elog(ERROR, "unknown agtype scalar type");
    }
}

/*
 * Produce an agtype string literal, properly escaping characters in the text.
 */
static void escape_agtype(StringInfo buf, const char *str)
{
    const char *p;

    appendStringInfoCharMacro(buf, '"');
    for (p = str; *p; p++)
    {
        switch (*p)
        {
        case '\b':
            appendStringInfoString(buf, "\\b");
            break;
        case '\f':
            appendStringInfoString(buf, "\\f");
            break;
        case '\n':
            appendStringInfoString(buf, "\\n");
            break;
        case '\r':
            appendStringInfoString(buf, "\\r");
            break;
        case '\t':
            appendStringInfoString(buf, "\\t");
            break;
        case '"':
            appendStringInfoString(buf, "\\\"");
            break;
        case '\\':
            appendStringInfoString(buf, "\\\\");
            break;
        default:
            if ((unsigned char)*p < ' ')
                appendStringInfo(buf, "\\u%04x", (int)*p);
            else
                appendStringInfoCharMacro(buf, *p);
            break;
        }
    }
    appendStringInfoCharMacro(buf, '"');
}

static bool is_decimal_needed(char *numstr)
{
    int i;

    Assert(numstr);

    i = (numstr[0] == '-') ? 1 : 0;

    while (numstr[i] != '\0')
    {
        if (numstr[i] < '0' || numstr[i] > '9')
            return false;

        i++;
    }

    return true;
}

/*
 * For agtype we always want the de-escaped value - that's what's in token
 */
static void agtype_in_scalar(void *pstate, char *token,
                             agtype_token_type tokentype)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;
    agtype_value v;

    switch (tokentype)
    {
    case AGTYPE_TOKEN_STRING:
        Assert(token != NULL);
        v.type = AGTV_STRING;
        v.val.string.len = check_string_length(strlen(token));
        v.val.string.val = token;
        break;
    case AGTYPE_TOKEN_INTEGER:
        Assert(token != NULL);
        v.type = AGTV_INTEGER;
        scanint8(token, false, &v.val.int_value);
        break;
    case AGTYPE_TOKEN_FLOAT:
        Assert(token != NULL);
        v.type = AGTV_FLOAT;
        v.val.float_value = float8in_internal(token, NULL, "double precision",
                                              token);
        break;
    case AGTYPE_TOKEN_TRUE:
        v.type = AGTV_BOOL;
        v.val.boolean = true;
        break;
    case AGTYPE_TOKEN_FALSE:
        v.type = AGTV_BOOL;
        v.val.boolean = false;
        break;
    case AGTYPE_TOKEN_NULL:
        v.type = AGTV_NULL;
        break;
    default:
        /* should not be possible */
        elog(ERROR, "invalid agtype token type");
        break;
    }

    if (_state->parse_state == NULL)
    {
        /* single scalar */
        agtype_value va;

        va.type = AGTV_ARRAY;
        va.val.array.raw_scalar = true;
        va.val.array.num_elems = 1;

        _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_ARRAY,
                                        &va);
        _state->res = push_agtype_value(&_state->parse_state, WAGT_ELEM, &v);
        _state->res = push_agtype_value(&_state->parse_state, WAGT_END_ARRAY,
                                        NULL);
    }
    else
    {
        agtype_value *o = &_state->parse_state->cont_val;

        switch (o->type)
        {
        case AGTV_ARRAY:
            _state->res = push_agtype_value(&_state->parse_state, WAGT_ELEM,
                                            &v);
            break;
        case AGTV_OBJECT:
            _state->res = push_agtype_value(&_state->parse_state, WAGT_VALUE,
                                            &v);
            break;
        default:
            elog(ERROR, "unexpected parent of nested structure");
        }
    }
}

/*
 * agtype_to_cstring
 *     Converts agtype value to a C-string.
 *
 * If 'out' argument is non-null, the resulting C-string is stored inside the
 * StringBuffer.  The resulting string is always returned.
 *
 * A typical case for passing the StringInfo in rather than NULL is where the
 * caller wants access to the len attribute without having to call strlen, e.g.
 * if they are converting it to a text* object.
 */
char *agtype_to_cstring(StringInfo out, agtype_container *in,
                        int estimated_len)
{
    return agtype_to_cstring_worker(out, in, estimated_len, false);
}

/*
 * same thing but with indentation turned on
 */
char *agtype_to_cstring_indent(StringInfo out, agtype_container *in,
                               int estimated_len)
{
    return agtype_to_cstring_worker(out, in, estimated_len, true);
}

/*
 * common worker for above two functions
 */
static char *agtype_to_cstring_worker(StringInfo out, agtype_container *in,
                                      int estimated_len, bool indent)
{
    bool first = true;
    agtype_iterator *it;
    agtype_value v;
    agtype_iterator_token type = WAGT_DONE;
    int level = 0;
    bool redo_switch = false;

    /* If we are indenting, don't add a space after a comma */
    int ispaces = indent ? 1 : 2;

    /*
     * Don't indent the very first item. This gets set to the indent flag at
     * the bottom of the loop.
     */
    bool use_indent = false;
    bool raw_scalar = false;
    bool last_was_key = false;

    if (out == NULL)
        out = makeStringInfo();

    enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

    it = agtype_iterator_init(in);

    while (redo_switch ||
           ((type = agtype_iterator_next(&it, &v, false)) != WAGT_DONE))
    {
        redo_switch = false;
        switch (type)
        {
        case WAGT_BEGIN_ARRAY:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);

            if (!v.val.array.raw_scalar)
            {
                add_indent(out, use_indent && !last_was_key, level);
                appendStringInfoCharMacro(out, '[');
            }
            else
            {
                raw_scalar = true;
            }

            first = true;
            level++;
            break;
        case WAGT_BEGIN_OBJECT:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);

            add_indent(out, use_indent && !last_was_key, level);
            appendStringInfoCharMacro(out, '{');

            first = true;
            level++;
            break;
        case WAGT_KEY:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);
            first = true;

            add_indent(out, use_indent, level);

            /* agtype rules guarantee this is a string */
            agtype_put_escaped_value(out, &v);
            appendBinaryStringInfo(out, ": ", 2);

            type = agtype_iterator_next(&it, &v, false);
            if (type == WAGT_VALUE)
            {
                first = false;
                agtype_put_escaped_value(out, &v);
            }
            else
            {
                Assert(type == WAGT_BEGIN_OBJECT || type == WAGT_BEGIN_ARRAY);

                /*
                 * We need to rerun the current switch() since we need to
                 * output the object which we just got from the iterator
                 * before calling the iterator again.
                 */
                redo_switch = true;
            }
            break;
        case WAGT_ELEM:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);
            first = false;

            if (!raw_scalar)
                add_indent(out, use_indent, level);
            agtype_put_escaped_value(out, &v);
            break;
        case WAGT_END_ARRAY:
            level--;
            if (!raw_scalar)
            {
                add_indent(out, use_indent, level);
                appendStringInfoCharMacro(out, ']');
            }
            first = false;
            break;
        case WAGT_END_OBJECT:
            level--;
            add_indent(out, use_indent, level);
            appendStringInfoCharMacro(out, '}');
            first = false;
            break;
        default:
            elog(ERROR, "unknown agtype iterator token type");
        }
        use_indent = indent;
        last_was_key = redo_switch;
    }

    Assert(level == 0);

    return out->data;
}

static void add_indent(StringInfo out, bool indent, int level)
{
    if (indent)
    {
        int i;

        appendStringInfoCharMacro(out, '\n');
        for (i = 0; i < level; i++)
            appendBinaryStringInfo(out, "    ", 4);
    }
}

/*
 * Determine how we want to render values of a given type in datum_to_agtype.
 *
 * Given the datatype OID, return its agt_type_category, as well as the type's
 * output function OID.  If the returned category is AGT_TYPE_JSONCAST,
 * we return the OID of the relevant cast function instead.
 */
static void agtype_categorize_type(Oid typoid, agt_type_category *tcategory,
                                   Oid *outfuncoid)
{
    bool typisvarlena;

    /* Look through any domain */
    typoid = getBaseType(typoid);

    *outfuncoid = InvalidOid;

    /*
     * We need to get the output function for everything except date and
     * timestamp types, booleans, array and composite types, json and jsonb,
     * and non-builtin types where there's a cast to json. In this last case
     * we return the oid of the cast function instead.
     */

    switch (typoid)
    {
    case BOOLOID:
        *tcategory = AGT_TYPE_BOOL;
        break;

    case INT2OID:
    case INT4OID:
    case INT8OID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_INTEGER;
        break;

    case FLOAT8OID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_FLOAT;
        break;

    case FLOAT4OID:
    case NUMERICOID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_NUMERIC;
        break;

    case DATEOID:
        *tcategory = AGT_TYPE_DATE;
        break;

    case TIMESTAMPOID:
        *tcategory = AGT_TYPE_TIMESTAMP;
        break;

    case TIMESTAMPTZOID:
        *tcategory = AGT_TYPE_TIMESTAMPTZ;
        break;

    case JSONBOID:
        *tcategory = AGT_TYPE_JSONB;
        break;

    case JSONOID:
        *tcategory = AGT_TYPE_JSON;
        break;

    default:
        /* Check for arrays and composites */
        if (typoid == AGTYPEOID)
        {
            *tcategory = AGT_TYPE_AGTYPE;
        }
        else if (OidIsValid(get_element_type(typoid)) ||
                 typoid == ANYARRAYOID || typoid == RECORDARRAYOID)
        {
            *tcategory = AGT_TYPE_ARRAY;
        }
        else if (type_is_rowtype(typoid)) /* includes RECORDOID */
        {
            *tcategory = AGT_TYPE_COMPOSITE;
        }
        else
        {
            /* It's probably the general case ... */
            *tcategory = AGT_TYPE_OTHER;

            /*
             * but first let's look for a cast to json (note: not to
             * jsonb) if it's not built-in.
             */
            if (typoid >= FirstNormalObjectId)
            {
                Oid castfunc;
                CoercionPathType ctype;

                ctype = find_coercion_pathway(JSONOID, typoid,
                                              COERCION_EXPLICIT, &castfunc);
                if (ctype == COERCION_PATH_FUNC && OidIsValid(castfunc))
                {
                    *tcategory = AGT_TYPE_JSONCAST;
                    *outfuncoid = castfunc;
                }
                else
                {
                    /* not a cast type, so just get the usual output func */
                    getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
                }
            }
            else
            {
                /* any other builtin type */
                getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
            }
            break;
        }
    }
}

/*
 * Turn a Datum into agtype, adding it to the result agtype_in_state.
 *
 * tcategory and outfuncoid are from a previous call to agtype_categorize_type,
 * except that if is_null is true then they can be invalid.
 *
 * If key_scalar is true, the value is stored as a key, so insist
 * it's of an acceptable type, and force it to be a AGTV_STRING.
 */
static void datum_to_agtype(Datum val, bool is_null, agtype_in_state *result,
                            agt_type_category tcategory, Oid outfuncoid,
                            bool key_scalar)
{
    char *outputstr;
    bool numeric_error;
    agtype_value agtv;
    bool scalar_agtype = false;

    check_stack_depth();

    /* Convert val to an agtype_value in agtv (in most cases) */
    if (is_null)
    {
        Assert(!key_scalar);
        agtv.type = AGTV_NULL;
    }
    else if (key_scalar &&
             (tcategory == AGT_TYPE_ARRAY || tcategory == AGT_TYPE_COMPOSITE ||
              tcategory == AGT_TYPE_JSON || tcategory == AGT_TYPE_JSONB ||
              tcategory == AGT_TYPE_JSONCAST))
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg(
                 "key value must be scalar, not array, composite, or json")));
    }
    else
    {
        if (tcategory == AGT_TYPE_JSONCAST)
            val = OidFunctionCall1(outfuncoid, val);

        switch (tcategory)
        {
        case AGT_TYPE_ARRAY:
            array_to_agtype_internal(val, result);
            break;
        case AGT_TYPE_COMPOSITE:
            composite_to_agtype(val, result);
            break;
        case AGT_TYPE_BOOL:
            if (key_scalar)
            {
                outputstr = DatumGetBool(val) ? "true" : "false";
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                agtv.type = AGTV_BOOL;
                agtv.val.boolean = DatumGetBool(val);
            }
            break;
        case AGT_TYPE_INTEGER:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                Datum intd;

                intd = DirectFunctionCall1(int8in, CStringGetDatum(outputstr));
                agtv.type = AGTV_INTEGER;
                agtv.val.int_value = DatumGetInt64(intd);
                pfree(outputstr);
            }
            break;
        case AGT_TYPE_FLOAT:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                agtv.type = AGTV_FLOAT;
                agtv.val.float_value = DatumGetFloat8(val);
            }
            break;
        case AGT_TYPE_NUMERIC:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                /* always quote keys */
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                /*
                 * Make it numeric if it's a valid agtype number, otherwise
                 * a string. Invalid numeric output will always have an
                 * 'N' or 'n' in it (I think).
                 */
                numeric_error = (strchr(outputstr, 'N') != NULL ||
                                 strchr(outputstr, 'n') != NULL);
                if (!numeric_error)
                {
                    Datum numd;

                    agtv.type = AGTV_NUMERIC;
                    numd = DirectFunctionCall3(numeric_in,
                                               CStringGetDatum(outputstr),
                                               ObjectIdGetDatum(InvalidOid),
                                               Int32GetDatum(-1));
                    agtv.val.numeric = DatumGetNumeric(numd);
                    pfree(outputstr);
                }
                else
                {
                    agtv.type = AGTV_STRING;
                    agtv.val.string.len = strlen(outputstr);
                    agtv.val.string.val = outputstr;
                }
            }
            break;
        case AGT_TYPE_DATE:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val, DATEOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_TIMESTAMP:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val,
                                                         TIMESTAMPOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_TIMESTAMPTZ:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val,
                                                         TIMESTAMPTZOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_JSONCAST:
        case AGT_TYPE_JSON:
        {
            /*
             * Parse the json right into the existing result object.
             * We can handle it as an agtype because agtype is currently an
             * extension of json.
             * Unlike AGT_TYPE_JSONB, numbers will be stored as either
             * an integer or a float, not a numeric.
             */
            agtype_lex_context *lex;
            agtype_sem_action sem;
            text *json = DatumGetTextPP(val);

            lex = make_agtype_lex_context(json, true);

            memset(&sem, 0, sizeof(sem));

            sem.semstate = (void *)result;

            sem.object_start = agtype_in_object_start;
            sem.array_start = agtype_in_array_start;
            sem.object_end = agtype_in_object_end;
            sem.array_end = agtype_in_array_end;
            sem.scalar = agtype_in_scalar;
            sem.object_field_start = agtype_in_object_field_start;

            parse_agtype(lex, &sem);
        }
        break;
        case AGT_TYPE_AGTYPE:
        case AGT_TYPE_JSONB:
        {
            agtype *jsonb = DATUM_GET_AGTYPE_P(val);
            agtype_iterator *it;

            /*
             * val is actually jsonb datum but we can handle it as an agtype
             * datum because agtype is currently an extension of jsonb.
             */

            it = agtype_iterator_init(&jsonb->root);

            if (AGT_ROOT_IS_SCALAR(jsonb))
            {
                (void)agtype_iterator_next(&it, &agtv, true);
                Assert(agtv.type == AGTV_ARRAY);
                (void)agtype_iterator_next(&it, &agtv, true);
                scalar_agtype = true;
            }
            else
            {
                agtype_iterator_token type;

                while ((type = agtype_iterator_next(&it, &agtv, false)) !=
                       WAGT_DONE)
                {
                    if (type == WAGT_END_ARRAY || type == WAGT_END_OBJECT ||
                        type == WAGT_BEGIN_ARRAY || type == WAGT_BEGIN_OBJECT)
                    {
                        result->res = push_agtype_value(&result->parse_state,
                                                        type, NULL);
                    }
                    else
                    {
                        result->res = push_agtype_value(&result->parse_state,
                                                        type, &agtv);
                    }
                }
            }
        }
        break;
        default:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            agtv.type = AGTV_STRING;
            agtv.val.string.len = check_string_length(strlen(outputstr));
            agtv.val.string.val = outputstr;
            break;
        }
    }

    /* Now insert agtv into result, unless we did it recursively */
    if (!is_null && !scalar_agtype && tcategory >= AGT_TYPE_AGTYPE &&
        tcategory <= AGT_TYPE_JSONCAST)
    {
        /* work has been done recursively */
        return;
    }
    else if (result->parse_state == NULL)
    {
        /* single root scalar */
        agtype_value va;

        va.type = AGTV_ARRAY;
        va.val.array.raw_scalar = true;
        va.val.array.num_elems = 1;

        result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                        &va);
        result->res = push_agtype_value(&result->parse_state, WAGT_ELEM, &agtv);
        result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                        NULL);
    }
    else
    {
        agtype_value *o = &result->parse_state->cont_val;

        switch (o->type)
        {
        case AGTV_ARRAY:
            result->res = push_agtype_value(&result->parse_state, WAGT_ELEM,
                                            &agtv);
            break;
        case AGTV_OBJECT:
            result->res = push_agtype_value(&result->parse_state,
                                            key_scalar ? WAGT_KEY : WAGT_VALUE,
                                            &agtv);
            break;
        default:
            elog(ERROR, "unexpected parent of nested structure");
        }
    }
}

/*
 * Process a single dimension of an array.
 * If it's the innermost dimension, output the values, otherwise call
 * ourselves recursively to process the next dimension.
 */
static void array_dim_to_agtype(agtype_in_state *result, int dim, int ndims,
                                int *dims, Datum *vals, bool *nulls,
                                int *valcount, agt_type_category tcategory,
                                Oid outfuncoid)
{
    int i;

    Assert(dim < ndims);

    result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                    NULL);

    for (i = 1; i <= dims[dim]; i++)
    {
        if (dim + 1 == ndims)
        {
            datum_to_agtype(vals[*valcount], nulls[*valcount], result,
                            tcategory, outfuncoid, false);
            (*valcount)++;
        }
        else
        {
            array_dim_to_agtype(result, dim + 1, ndims, dims, vals, nulls,
                                valcount, tcategory, outfuncoid);
        }
    }

    result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                    NULL);
}

/*
 * Turn an array into agtype.
 */
static void array_to_agtype_internal(Datum array, agtype_in_state *result)
{
    ArrayType *v = DatumGetArrayTypeP(array);
    Oid element_type = ARR_ELEMTYPE(v);
    int *dim;
    int ndim;
    int nitems;
    int count = 0;
    Datum *elements;
    bool *nulls;
    int16 typlen;
    bool typbyval;
    char typalign;
    agt_type_category tcategory;
    Oid outfuncoid;

    ndim = ARR_NDIM(v);
    dim = ARR_DIMS(v);
    nitems = ArrayGetNItems(ndim, dim);

    if (nitems <= 0)
    {
        result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                        NULL);
        result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                        NULL);
        return;
    }

    get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

    agtype_categorize_type(element_type, &tcategory, &outfuncoid);

    deconstruct_array(v, element_type, typlen, typbyval, typalign, &elements,
                      &nulls, &nitems);

    array_dim_to_agtype(result, 0, ndim, dim, elements, nulls, &count,
                        tcategory, outfuncoid);

    pfree(elements);
    pfree(nulls);
}

/*
 * Turn a composite / record into agtype.
 */
static void composite_to_agtype(Datum composite, agtype_in_state *result)
{
    HeapTupleHeader td;
    Oid tup_type;
    int32 tup_typmod;
    TupleDesc tupdesc;
    HeapTupleData tmptup, *tuple;
    int i;

    td = DatumGetHeapTupleHeader(composite);

    /* Extract rowtype info and find a tupdesc */
    tup_type = HeapTupleHeaderGetTypeId(td);
    tup_typmod = HeapTupleHeaderGetTypMod(td);
    tupdesc = lookup_rowtype_tupdesc(tup_type, tup_typmod);

    /* Build a temporary HeapTuple control structure */
    tmptup.t_len = HeapTupleHeaderGetDatumLength(td);
    tmptup.t_data = td;
    tuple = &tmptup;

    result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_OBJECT,
                                    NULL);

    for (i = 0; i < tupdesc->natts; i++)
    {
        Datum val;
        bool isnull;
        char *attname;
        agt_type_category tcategory;
        Oid outfuncoid;
        agtype_value v;
        Form_pg_attribute att = TupleDescAttr(tupdesc, i);

        if (att->attisdropped)
            continue;

        attname = NameStr(att->attname);

        v.type = AGTV_STRING;
        /*
         * don't need check_string_length here
         * - can't exceed maximum name length
         */
        v.val.string.len = strlen(attname);
        v.val.string.val = attname;

        result->res = push_agtype_value(&result->parse_state, WAGT_KEY, &v);

        val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

        if (isnull)
        {
            tcategory = AGT_TYPE_NULL;
            outfuncoid = InvalidOid;
        }
        else
        {
            agtype_categorize_type(att->atttypid, &tcategory, &outfuncoid);
        }

        datum_to_agtype(val, isnull, result, tcategory, outfuncoid, false);
    }

    result->res = push_agtype_value(&result->parse_state, WAGT_END_OBJECT,
                                    NULL);
    ReleaseTupleDesc(tupdesc);
}

/*
 * Append agtype text for "val" to "result".
 *
 * This is just a thin wrapper around datum_to_agtype.  If the same type
 * will be printed many times, avoid using this; better to do the
 * agtype_categorize_type lookups only once.
 */
static void add_agtype(Datum val, bool is_null, agtype_in_state *result,
                       Oid val_type, bool key_scalar)
{
    agt_type_category tcategory;
    Oid outfuncoid;

    if (val_type == InvalidOid)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("could not determine input data type")));
    }

    if (is_null)
    {
        tcategory = AGT_TYPE_NULL;
        outfuncoid = InvalidOid;
    }
    else
    {
        agtype_categorize_type(val_type, &tcategory, &outfuncoid);
    }

    datum_to_agtype(val, is_null, result, tcategory, outfuncoid, key_scalar);
}

PG_FUNCTION_INFO_V1(agtype_build_list);

/*
 * SQL function agtype_build_list(variadic "any")
 */
Datum agtype_build_list(PG_FUNCTION_ARGS)
{
    int nargs;
    int i;
    agtype_in_state result;
    Datum *args;
    bool *nulls;
    Oid *types;

    /*build argument values to build the array*/
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    if (nargs < 0)
        PG_RETURN_NULL();

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY,
                                   NULL);

    for (i = 0; i < nargs; i++)
        add_agtype(args[i], nulls[i], &result, types[i], false);

    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(result.res));
}

PG_FUNCTION_INFO_V1(agtype_build_list_noargs);

/*
 * degenerate case of agtype_build_list where it gets 0 arguments.
 */
Datum agtype_build_list_noargs(PG_FUNCTION_ARGS)
{
    agtype_in_state result;

    memset(&result, 0, sizeof(agtype_in_state));

    push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY, NULL);
    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(result.res));
}
