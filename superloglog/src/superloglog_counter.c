#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "postgres.h"
#include "fmgr.h"
#include "superloglog.h"
#include "utils/builtins.h"
#include "utils/bytea.h"
#include "utils/lsyscache.h"
#include "lib/stringinfo.h"
#include "libpq/pqformat.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define VAL(CH)         ((CH) - '0')
#define DIG(VAL)        ((VAL) + '0')

#define DEFAULT_ERROR       0.025

PG_FUNCTION_INFO_V1(superloglog_add_item);
PG_FUNCTION_INFO_V1(superloglog_add_item_agg);
PG_FUNCTION_INFO_V1(superloglog_add_item_agg2);

PG_FUNCTION_INFO_V1(superloglog_get_estimate);
PG_FUNCTION_INFO_V1(superloglog_size);
PG_FUNCTION_INFO_V1(superloglog_init);
PG_FUNCTION_INFO_V1(superloglog_reset);
PG_FUNCTION_INFO_V1(superloglog_in);
PG_FUNCTION_INFO_V1(superloglog_out);
PG_FUNCTION_INFO_V1(superloglog_rect);
PG_FUNCTION_INFO_V1(superloglog_send);
PG_FUNCTION_INFO_V1(superloglog_length);

Datum superloglog_add_item(PG_FUNCTION_ARGS);
Datum superloglog_add_item_agg(PG_FUNCTION_ARGS);
Datum superloglog_add_item_agg2(PG_FUNCTION_ARGS);

Datum superloglog_get_estimate(PG_FUNCTION_ARGS);

Datum superloglog_size(PG_FUNCTION_ARGS);
Datum superloglog_init(PG_FUNCTION_ARGS);
Datum superloglog_reset(PG_FUNCTION_ARGS);
Datum superloglog_in(PG_FUNCTION_ARGS);
Datum superloglog_out(PG_FUNCTION_ARGS);
Datum superloglog_recv(PG_FUNCTION_ARGS);
Datum superloglog_send(PG_FUNCTION_ARGS);
Datum superloglog_length(PG_FUNCTION_ARGS);

Datum
superloglog_add_item(PG_FUNCTION_ARGS)
{

    SuperLogLogCounter sloglog;

    /* requires the estimator to be already created */
    if (PG_ARGISNULL(0))
        elog(ERROR, "superloglog counter must not be NULL");

    /* if the element is not NULL, add it to the estimator (i.e. skip NULLs) */
    if (! PG_ARGISNULL(1)) {

        Oid         element_type = get_fn_expr_argtype(fcinfo->flinfo, 1);
        Datum       element = PG_GETARG_DATUM(1);
        int16       typlen;
        bool        typbyval;
        char        typalign;

        /* estimator (we know it's not a NULL value) */
        sloglog = (SuperLogLogCounter)PG_GETARG_BYTEA_P(0);

        /* TODO The requests for type info shouldn't be a problem (thanks to lsyscache),
         * but if it turns out to have a noticeable impact it's possible to cache that
         * between the calls (in the estimator). */

        /* get type information for the second parameter (anyelement item) */
        get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

        /* if it's not a varlena type, just use the value directly */
        if (typlen != -1) {
            /* use the whole Datum, zero bytes make no difference anyway */
            superloglog_add_element(sloglog, (char*)&element, sizeof(Datum));
        } else {
            /* in-place update works only if executed as aggregate */
            superloglog_add_element(sloglog, VARDATA(element), VARSIZE(element) - VARHDRSZ);
        }

    }

    PG_RETURN_VOID();

}

Datum
superloglog_add_item_agg(PG_FUNCTION_ARGS)
{

    SuperLogLogCounter sloglog;
    float errorRate; /* required error rate */

    /* info for anyelement */
    Oid         element_type = get_fn_expr_argtype(fcinfo->flinfo, 1);
    Datum       element = PG_GETARG_DATUM(1);
    int16       typlen;
    bool        typbyval;
    char        typalign;

    /* create a new estimator (with requested error rate) or reuse the existing one */
    if (PG_ARGISNULL(0)) {

        errorRate = PG_GETARG_FLOAT4(2);

        /* error rate between 0 and 1 (not 0) */
        if ((errorRate <= 0) || (errorRate > 1))
            elog(ERROR, "error rate has to be between 0 and 1");

        sloglog = superloglog_create(errorRate);

    } else { /* existing estimator */
        sloglog = (SuperLogLogCounter)PG_GETARG_BYTEA_P(0);
    }

    /* add the item to the estimator (skip NULLs) */
    if (! PG_ARGISNULL(1)) {

        /* TODO The requests for type info shouldn't be a problem (thanks to lsyscache),
         * but if it turns out to have a noticeable impact it's possible to cache that
         * between the calls (in the estimator). */
        
        /* get type information for the second parameter (anyelement item) */
        get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

        /* if it's not a varlena type, just use the value directly */
        if (typlen != -1) {
            /* use the whole Datum, zero bytes make no difference anyway */
            superloglog_add_element(sloglog, (char*)&element, sizeof(Datum));
        } else {
            /* in-place update works only if executed as aggregate */
            superloglog_add_element(sloglog, VARDATA(element), VARSIZE(element) - VARHDRSZ);
        }
    }

    /* return the updated bytea */
    PG_RETURN_BYTEA_P(sloglog);
    
}

Datum
superloglog_add_item_agg2(PG_FUNCTION_ARGS)
{

    SuperLogLogCounter sloglog;

    /* info for anyelement */
    Oid         element_type = get_fn_expr_argtype(fcinfo->flinfo, 1);
    Datum       element = PG_GETARG_DATUM(1);
    int16       typlen;
    bool        typbyval;
    char        typalign;

    /* create a new estimator (with requested error rate) or reuse the existing one */
    if (PG_ARGISNULL(0)) {
        sloglog = superloglog_create(DEFAULT_ERROR);
    } else { /* existing estimator */
        sloglog = (SuperLogLogCounter)PG_GETARG_BYTEA_P(0);
    }

    /* add the item to the estimator (skip NULLs) */
    if (! PG_ARGISNULL(1)) {

        /* TODO The requests for type info shouldn't be a problem (thanks to lsyscache),
         * but if it turns out to have a noticeable impact it's possible to cache that
         * between the calls (in the estimator). */
        
        /* get type information for the second parameter (anyelement item) */
        get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

        /* if it's not a varlena type, just use the value directly */
        if (typlen != -1) {
            /* use the whole Datum, zero bytes make no difference anyway */
            superloglog_add_element(sloglog, (char*)&element, sizeof(Datum));
        } else {
            /* in-place update works only if executed as aggregate */
            superloglog_add_element(sloglog, VARDATA(element), VARSIZE(element) - VARHDRSZ);
        }
    }

    /* return the updated bytea */
    PG_RETURN_BYTEA_P(sloglog);
    
}

Datum
superloglog_get_estimate(PG_FUNCTION_ARGS)
{
  
    int estimate;
    SuperLogLogCounter loglog = (SuperLogLogCounter)PG_GETARG_BYTEA_P(0);
    
    /* in-place update works only if executed as aggregate */
    estimate = superloglog_estimate(loglog);
    
    /* return the updated bytea */
    PG_RETURN_FLOAT4(estimate);

}

Datum
superloglog_init(PG_FUNCTION_ARGS)
{
      SuperLogLogCounter loglog;

      float errorRate; /* required error rate */
  
      errorRate = PG_GETARG_FLOAT4(0);
      
      /* error rate between 0 and 1 (not 0) */
      if ((errorRate <= 0) || (errorRate > 1)) {
          elog(ERROR, "error rate has to be between 0 and 1");
      }
      
      loglog = superloglog_create(errorRate);

      PG_RETURN_BYTEA_P(loglog);
}

Datum
superloglog_size(PG_FUNCTION_ARGS)
{

      float errorRate; /* required error rate */
  
      errorRate = PG_GETARG_FLOAT4(0);
      
      /* error rate between 0 and 1 (not 0) */
      if ((errorRate <= 0) || (errorRate > 1)) {
          elog(ERROR, "error rate has to be between 0 and 1");
      }
      
      PG_RETURN_INT32(superloglog_get_size(errorRate));      
}

Datum
superloglog_length(PG_FUNCTION_ARGS)
{
    PG_RETURN_INT32(VARSIZE((SuperLogLogCounter)PG_GETARG_BYTEA_P(0)));
}

Datum
superloglog_reset(PG_FUNCTION_ARGS)
{
	superloglog_reset_internal(((SuperLogLogCounter)PG_GETARG_BYTEA_P(0)));
	PG_RETURN_VOID();
}


/*
 *		byteain			- converts from printable representation of byte array
 *
 *		Non-printable characters must be passed as '\nnn' (octal) and are
 *		converted to internal form.  '\' must be passed as '\\'.
 *		ereport(ERROR, ...) if bad form.
 *
 *		BUGS:
 *				The input is scanned twice.
 *				The error checking of input is minimal.
 */
Datum
superloglog_in(PG_FUNCTION_ARGS)
{
	char	   *inputText = PG_GETARG_CSTRING(0);
	char	   *tp;
	char	   *rp;
	int			bc;
	bytea	   *result;

	/* Recognize hex input */
	if (inputText[0] == '\\' && inputText[1] == 'x')
	{
		size_t		len = strlen(inputText);

		bc = (len - 2) / 2 + VARHDRSZ;	/* maximum possible length */
		result = palloc(bc);
		bc = hex_decode(inputText + 2, len - 2, VARDATA(result));
		SET_VARSIZE(result, bc + VARHDRSZ);		/* actual length */

		PG_RETURN_BYTEA_P(result);
	}

	/* Else, it's the traditional escaped style */
	for (bc = 0, tp = inputText; *tp != '\0'; bc++)
	{
		if (tp[0] != '\\')
			tp++;
		else if ((tp[0] == '\\') &&
				 (tp[1] >= '0' && tp[1] <= '3') &&
				 (tp[2] >= '0' && tp[2] <= '7') &&
				 (tp[3] >= '0' && tp[3] <= '7'))
			tp += 4;
		else if ((tp[0] == '\\') &&
				 (tp[1] == '\\'))
			tp += 2;
		else
		{
			/*
			 * one backslash, not followed by another or ### valid octal
			 */
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("invalid input syntax for type bytea")));
		}
	}

	bc += VARHDRSZ;

	result = (bytea *) palloc(bc);
	SET_VARSIZE(result, bc);

	tp = inputText;
	rp = VARDATA(result);
	while (*tp != '\0')
	{
		if (tp[0] != '\\')
			*rp++ = *tp++;
		else if ((tp[0] == '\\') &&
				 (tp[1] >= '0' && tp[1] <= '3') &&
				 (tp[2] >= '0' && tp[2] <= '7') &&
				 (tp[3] >= '0' && tp[3] <= '7'))
		{
			bc = VAL(tp[1]);
			bc <<= 3;
			bc += VAL(tp[2]);
			bc <<= 3;
			*rp++ = bc + VAL(tp[3]);

			tp += 4;
		}
		else if ((tp[0] == '\\') &&
				 (tp[1] == '\\'))
		{
			*rp++ = '\\';
			tp += 2;
		}
		else
		{
			/*
			 * We should never get here. The first pass should not allow it.
			 */
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("invalid input syntax for type bytea")));
		}
	}

	PG_RETURN_BYTEA_P(result);
}

/*
 *		byteaout		- converts to printable representation of byte array
 *
 *		In the traditional escaped format, non-printable characters are
 *		printed as '\nnn' (octal) and '\' as '\\'.
 */
Datum
superloglog_out(PG_FUNCTION_ARGS)
{
	bytea	   *vlena = PG_GETARG_BYTEA_PP(0);
	char	   *result;
	char	   *rp;

	if (bytea_output == BYTEA_OUTPUT_HEX)
	{
		/* Print hex format */
		rp = result = palloc(VARSIZE_ANY_EXHDR(vlena) * 2 + 2 + 1);
		*rp++ = '\\';
		*rp++ = 'x';
		rp += hex_encode(VARDATA_ANY(vlena), VARSIZE_ANY_EXHDR(vlena), rp);
	}
	else if (bytea_output == BYTEA_OUTPUT_ESCAPE)
	{
		/* Print traditional escaped format */
		char	   *vp;
		int			len;
		int			i;

		len = 1;				/* empty string has 1 char */
		vp = VARDATA_ANY(vlena);
		for (i = VARSIZE_ANY_EXHDR(vlena); i != 0; i--, vp++)
		{
			if (*vp == '\\')
				len += 2;
			else if ((unsigned char) *vp < 0x20 || (unsigned char) *vp > 0x7e)
				len += 4;
			else
				len++;
		}
		rp = result = (char *) palloc(len);
		vp = VARDATA_ANY(vlena);
		for (i = VARSIZE_ANY_EXHDR(vlena); i != 0; i--, vp++)
		{
			if (*vp == '\\')
			{
				*rp++ = '\\';
				*rp++ = '\\';
			}
			else if ((unsigned char) *vp < 0x20 || (unsigned char) *vp > 0x7e)
			{
				int			val;	/* holds unprintable chars */

				val = *vp;
				rp[0] = '\\';
				rp[3] = DIG(val & 07);
				val >>= 3;
				rp[2] = DIG(val & 07);
				val >>= 3;
				rp[1] = DIG(val & 03);
				rp += 4;
			}
			else
				*rp++ = *vp;
		}
	}
	else
	{
		elog(ERROR, "unrecognized bytea_output setting: %d",
			 bytea_output);
		rp = result = NULL;		/* keep compiler quiet */
	}
	*rp = '\0';
	PG_RETURN_CSTRING(result);
}

/*
 *		bytearecv			- converts external binary format to bytea
 */
Datum
superloglog_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	bytea	   *result;
	int			nbytes;

	nbytes = buf->len - buf->cursor;
	result = (bytea *) palloc(nbytes + VARHDRSZ);
	SET_VARSIZE(result, nbytes + VARHDRSZ);
	pq_copymsgbytes(buf, VARDATA(result), nbytes);
	PG_RETURN_BYTEA_P(result);
}

/*
 *		byteasend			- converts bytea to binary format
 *
 * This is a special case: just copy the input...
 */
Datum
superloglog_send(PG_FUNCTION_ARGS)
{
	bytea	   *vlena = PG_GETARG_BYTEA_P_COPY(0);

	PG_RETURN_BYTEA_P(vlena);
}