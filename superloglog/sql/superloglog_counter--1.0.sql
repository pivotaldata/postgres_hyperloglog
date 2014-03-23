-- LogLog 

-- LogLog counter (shell type)
CREATE TYPE superloglog_estimator;

-- get estimator size for the requested error rate
CREATE FUNCTION superloglog_size(error_rate real) RETURNS int
     AS 'MODULE_PATHNAME', 'superloglog_size'
     LANGUAGE C;

-- creates a new LogLog estimator with a given a desired error rate limit
CREATE FUNCTION superloglog_init(error_rate real) RETURNS superloglog_estimator
     AS 'MODULE_PATHNAME', 'superloglog_init'
     LANGUAGE C;

-- add an item to the estimator
CREATE FUNCTION superloglog_add_item(counter superloglog_estimator, item anyelement) RETURNS void
     AS 'MODULE_PATHNAME', 'superloglog_add_item'
     LANGUAGE C;

-- get current estimate of the distinct values (as a real number)
CREATE FUNCTION superloglog_get_estimate(counter superloglog_estimator) RETURNS real
     AS 'MODULE_PATHNAME', 'superloglog_get_estimate'
     LANGUAGE C STRICT;

-- reset the estimator (start counting from the beginning)
CREATE FUNCTION superloglog_reset(counter superloglog_estimator) RETURNS void
     AS 'MODULE_PATHNAME', 'superloglog_reset'
     LANGUAGE C STRICT;

-- length of the estimator (about the same as superloglog_size with existing estimator)
CREATE FUNCTION length(counter superloglog_estimator) RETURNS int
     AS 'MODULE_PATHNAME', 'superloglog_length'
     LANGUAGE C STRICT;

/* functions for aggregate functions */

CREATE FUNCTION superloglog_add_item_agg(counter superloglog_estimator, item anyelement, error_rate real) RETURNS superloglog_estimator
     AS 'MODULE_PATHNAME', 'superloglog_add_item_agg'
     LANGUAGE C;

CREATE FUNCTION superloglog_add_item_agg2(counter superloglog_estimator, item anyelement) RETURNS superloglog_estimator
     AS 'MODULE_PATHNAME', 'superloglog_add_item_agg2'
     LANGUAGE C;

/* input/output functions */

CREATE FUNCTION superloglog_in(value cstring) RETURNS superloglog_estimator
     AS 'MODULE_PATHNAME', 'superloglog_in'
     LANGUAGE C STRICT;

CREATE FUNCTION superloglog_out(counter superloglog_estimator) RETURNS cstring
     AS 'MODULE_PATHNAME', 'superloglog_out'
     LANGUAGE C STRICT;

-- actual LogLog counter data type
CREATE TYPE superloglog_estimator (
    INPUT = superloglog_in,
    OUTPUT = superloglog_out,
    LIKE  = bytea
);

-- LogLog based aggregate
-- items / error rate / number of items
CREATE AGGREGATE superloglog_distinct(item anyelement, error_rate real)
(
    sfunc = superloglog_add_item_agg,
    stype = superloglog_estimator,
    finalfunc = superloglog_get_estimate
);

CREATE AGGREGATE superloglog_distinct(item anyelement)
(
    sfunc = superloglog_add_item_agg2,
    stype = superloglog_estimator,
    finalfunc = superloglog_get_estimate
);
