/*
 * Original Author: Jon Nelson, 8 Nov 2010, jnelson@renesys.com
 * Current Author: Jon Nelson, jdnelson@dyn.com
 *
 *
 * Copyright (c) 2015, Dynamic Network Services, Inc.
 * all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *     * Neither the name of Dynamic Network Services, Inc. nor the
 *         names of its contributors may be used to endorse or promote products
 *         derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Dynamic Network Services, Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* postgresql includes */
#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
// #include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/palloc.h"
#include "mb/pg_wchar.h"

/* libidn includes */
#include <stringprep.h>
#include <idna.h>
#include <pr29.h>
#include <punycode.h>

/* libidn2 includes */
#include <idn2.h>

PG_MODULE_MAGIC;
void _PG_init(void);
void _PG_fini(void);

static short stringprep_version_bad = 0;

enum constant_scope {
    SCOPE_STRINGPREP = 1, /* start at 1 */
    SCOPE_IDNA,
    SCOPE_IDNA2,
    SCOPE_PUNYCODE, /* unused at the moment */
};

struct idn_constants_struct {
    enum constant_scope scope;
    const char *name;
    int value;
    const char *description;
};

static struct idn_constants_struct _constants[] = {
    {
        .scope = SCOPE_STRINGPREP,
        .name = "STRINGPREP_FLAG_NONE",
        .value = 0,
        .description = "A value representing no flags supplied.",
    },
    {
        .scope = SCOPE_STRINGPREP,
        .name = "STRINGPREP_FLAG_NO_NFKC",
        .value = STRINGPREP_NO_NFKC,
        .description = "Disable the NFKC normalization, as well as selecting "
                       "the non-NFKC case folding tables. Usually the profile "
                       "specifies BIDI and NFKC settings, and applications "
                       "should not override it unless in special situations.",
    },
    {
        .scope = SCOPE_STRINGPREP,
        .name = "STRINGPREP_FLAG_NO_BIDI",
        .value = STRINGPREP_NO_BIDI,
        .description = "Disable the BIDI step. Usually the profile specifies BIDI and NFKC settings, and applications should not override it unless in special situations.",
    },
    {
        .scope = SCOPE_STRINGPREP,
        .name = "STRINGPREP_FLAG_NO_UNASSIGNED",
        .value = STRINGPREP_NO_UNASSIGNED,
        .description = "Make the library return with an error if string contains unassigned characters according to profile.",
    },
    {
        .scope = SCOPE_IDNA,
        .name = "IDNA_FLAG_NONE",
        .value = 0,
        .description = "A value representing no flags supplied.",
    },
    {
        .scope = SCOPE_IDNA,
        .name = "IDNA_FLAG_ALLOW_UNASSIGNED",
        .value = IDNA_ALLOW_UNASSIGNED,
        .description = "Allow unassigned Unicode code points.",
    },
    {
        .scope = SCOPE_IDNA,
        .name = "IDNA_FLAG_USE_STD3_ASCII_RULES",
        .value = IDNA_USE_STD3_ASCII_RULES,
        .description = "Check output to make sure it is a STD3 conforming host name.",
    },
    {
        .scope = SCOPE_IDNA2,
        .name = "IDN2_FLAG_NONE",
        .value = 0,
        .description = "A value representing no flags supplied.",
    },
    {
        .scope = SCOPE_IDNA2,
        .name = "IDN2_FLAG_NFC_INPUT",
        .value = IDN2_NFC_INPUT,
        .description = "Apply NFC normalization on input.",
    },
    {
        .scope = SCOPE_IDNA2,
        .name = "IDN2_FLAG_ALABEL_ROUNDTRIP",
        .value = IDN2_ALABEL_ROUNDTRIP,
        .description = "Apply additional round-trip conversion of A-label inputs.",
    },
};

static int
constants_compare(const void *p1, const void *p2)
{
    struct idn_constants_struct *a, *b;
    a = (struct idn_constants_struct *) p1;
    b = (struct idn_constants_struct *) p2;
    if (a->scope < b->scope) {
        return -1;
    } else if (a->scope > b->scope) {
        return 1;
    }
    /* else */
    return pg_strcasecmp(a->name, b->name);
}

void _PG_init(void)
{
    /* sort constants */
    qsort(_constants,
          sizeof(_constants) / sizeof(struct idn_constants_struct),
          sizeof(struct idn_constants_struct),
          constants_compare);

    if (!stringprep_check_version(STRINGPREP_VERSION)) {
        stringprep_version_bad = 1;
    }
}

/* this is never called anyway, but it's good practice */
void _PG_fini(void)
{
}

/* define a type for internal utf8 functions to call */
typedef char *(*utf8_fn)(const char *, ssize_t, int32);

static int
_parse_constant(enum constant_scope scope, char *str)
{
    struct idn_constants_struct key, *res;

    key.scope = scope;
    key.name = str;

    res = bsearch(&key,
                  _constants,
                  sizeof(_constants) / sizeof(struct idn_constants_struct),
                  sizeof(struct idn_constants_struct),
                  constants_compare);
    if (res == NULL) {
        return  -1;
    }
    return res->value;
}

static int
parse_constant_multi(enum constant_scope scope, char *str)
{
    char *s, *orig;
    size_t slen = strlen(str);
    int ret, temp;

    ret = 0;

    orig = s = palloc(slen + 1);
    memcpy(s, str, slen+1);

    while (s) {
        s = strchr(str, '|');
        if (s) {
            *s = '\0';
        }

        temp = _parse_constant(scope, str);
        if (temp < 0) {
            pfree(orig);
            elog(ERROR, "Unknown constant name: %s", str);
        }
        ret |= temp;
        if (s) {
            str = s + 1;
        }
    }
    pfree(orig);
    return ret;
}

static int parse_text_arg_flags(text *arg, enum constant_scope scope) {
    /* turn arg into flags */
    int flags;
    char *arg_cstring = text_to_cstring(arg);
    flags = parse_constant_multi(scope, arg_cstring);
    pfree(arg_cstring);
    return flags;
}


static bool
ascii_check(const uint8_t *src, size_t slen)
{
    size_t i = 0;

    for (i = 0; i < slen; ++i) {
        /* 0x20 (space) through 0x7E, inclusive */
        if ((0x20 <= src[i]) && (src[i] <= 0x7E)) {
            ;
        } else {
            /* non-ascii or non-printing (or both) */
            return false;
        }
    }
    return true;
}

/* convert a TEXT argument to UTF-8
 * If the database encoding is SQL_ASCII, the contents are
 * simply validated (based upon comments found in src/backend/utils/mb/mbutils.c
 */
static char *text_to_utf8(text *arg, size_t *utf8_srclen, bool *needs_free, bool force_new)
{
    char *src, *utf8_src;
    size_t srclen;

    src = VARDATA_ANY(arg);
    srclen = VARSIZE_ANY_EXHDR(arg);

    /* we /may/ need to convert from (whatever encoding the db is in) to UTF-8 */
    utf8_src = (char *) pg_do_encoding_conversion((unsigned char *) src, srclen, GetDatabaseEncoding(), PG_UTF8);

    /* if utf8_src == src, no conversion happened, otherwise
    * the returned string is NULL-terminated
    */
    if (utf8_src == src) {
        if (force_new) {
            utf8_src = palloc(srclen + 1);
            memcpy(utf8_src, src, srclen);
            utf8_src[srclen] = '\0';
            *needs_free = true;
        } else {
            *needs_free = false;
        }
        *utf8_srclen = srclen;
    } else {
        *utf8_srclen = strlen(utf8_src);
        *needs_free = true;
    }
    return utf8_src;
}

static text *utf8_to_text(char *src, size_t srclen)
{
    char *dest;
    size_t destlen;
    text *ret;

    /* src is in UTF-8, but the db might not be */
    dest = (char *) pg_do_encoding_conversion((unsigned char *) src, srclen, PG_UTF8, GetDatabaseEncoding());

    /* if dest == src, that means no conversion took place */
    if (dest == src) {
        destlen = srclen;
    } else {
        destlen = strlen(dest);
    }

    ret = cstring_to_text_with_len(dest, destlen);

    /* we are done with dest. may need to deallocate */
    if (dest != src) {
        pfree(dest);
    }
    return ret;
}

static bool check_stringprep(void)
{
    if (stringprep_version_bad) {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("The version of the stringprep library and the header used during compile differ.")));
        /* technically, the return false is unnecessary as it's
         * not ever reached. If somebody were to change the
         * severity level, however, chaos would ensue, so
         * returning false here doesn't seem unreasonable
         */
        return false;
    }
    return true;
}

Datum libidn_stringprep(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(libidn_stringprep);
Datum libidn_stringprep(PG_FUNCTION_ARGS)
{
    char *utf8_src, *utf8_dest;
    text *ret;
    bool needs_free;
    char * profile_name;
    size_t utf8_srclen;
    Stringprep_profile_flags profile_flags = 0;
    Stringprep_rc rc;

    /* before we do anything else */
    if (!check_stringprep()) {
        /* actually, check_stringprep raises ERROR */
        PG_RETURN_NULL();
    }
    switch( PG_NARGS() ) {
        case 3:
            if (PG_ARGISNULL(2)) {
                profile_flags = 0;
            } else {
                char *arg3_cstring = text_to_cstring(PG_GETARG_TEXT_PP(2));
                profile_flags = parse_constant_multi(SCOPE_STRINGPREP, arg3_cstring);
                pfree(arg3_cstring);
            }
        case 2:
            if (PG_ARGISNULL(0) || PG_ARGISNULL(1)) {
                PG_RETURN_NULL();
            } else {
                /* the fourth param, 'true', specifies that we _always_ want
                * a zero-terminated string back
                */
                profile_name = text_to_cstring(PG_GETARG_TEXT_PP(1));
                utf8_src = text_to_utf8(PG_GETARG_TEXT_PP(0), &utf8_srclen, &needs_free, true);
            }
            break;
        default:
            elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }

    /* perform the stringprep conversion */
    rc = stringprep_profile(utf8_src, &utf8_dest, profile_name, profile_flags);

    /* we are done with utf8_src */
    if (needs_free) {
        pfree(utf8_src);
    }

    /* we are also done with profile_name */
    pfree(profile_name);

    /* check the results of the conversion */
    if (rc != STRINGPREP_OK) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error performing stringprep profile conversion: %s.",
                     stringprep_strerror(rc))
                )
               );
        PG_RETURN_NULL();
    }

    /* convert return value back to whatever the db encoding is */
    ret = utf8_to_text(utf8_dest, strlen(utf8_dest));

    /* we're done with utf8_dest */
    free(utf8_dest);

    /* done */
    PG_RETURN_TEXT_P(ret);
}



Datum idn_punycode_encode(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(idn_punycode_encode);
Datum idn_punycode_encode(PG_FUNCTION_ARGS)
{
    text *arg0;

    char *utf8_src, *dest;
    size_t destlen;
    size_t utf8_srclen, ucs4_len;
    text *ret;
    bool needs_free;
    punycode_uint *pu;
    int rc;

    /* before we do anything else */
    if (!check_stringprep()) {
        /* actually, check_stringprep raises ERROR */
        PG_RETURN_NULL();
    }
    if (PG_NARGS() != 1) {
        elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }
    /* while the function is defined as strict, this belts-and-suspenders
     * doesn't hurt
     */
    if (PG_ARGISNULL(0)) {
        PG_RETURN_NULL();
    }
    arg0 = PG_GETARG_TEXT_PP(0);

    utf8_src = text_to_utf8(arg0, &utf8_srclen, &needs_free, false);

    /* NOTE: utf8_src is not necessarily NUL-terminated */

    /* now we have to use stringprep to convert from utf8 to ucs4 */
    pu = stringprep_utf8_to_ucs4(utf8_src, utf8_srclen, &ucs4_len);

    /* we are done with utf8_src */
    if (needs_free) {
        pfree(utf8_src);
    }

    /* check result of ucs4 conversion */
    if (!pu) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error converting to from UTF-8 to UCS4.")));
        PG_RETURN_NULL();
    }

    /* need to allocate enough space for dest */
    /* FIXME: let's start with 3x. I couldn't find good material that provides
     * an upper-bound on the potential size expansion.
     */
    destlen = utf8_srclen * 3;
    dest = palloc(destlen + 1); /* add one for the NUL-terminator */

    /* we have to remember to free 'pu' */
    rc = punycode_encode(ucs4_len, pu, NULL, &destlen, dest);

    /* we are done with 'pu' */
    free(pu);

    /* check rc, etc. */
    if (rc != PUNYCODE_SUCCESS) {
        pfree(dest);
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error encountered converting to Punycode: %s", punycode_strerror(rc))));
        PG_RETURN_NULL();
    }

    /* NUL-terminate the result */
    dest[destlen] = '\0';

    /* the result is ASCII, but we can pretend it's UTF-8 for
     * purposes of returning data to the caller because
     * ASCII is a subset of UTF-8.
     */

    /* convert return value back to whatever the db encoding is */
    ret = utf8_to_text(dest, destlen);

    /* we aren't using dest */
    pfree(dest);

    /* done */
    PG_RETURN_TEXT_P(ret);
}

Datum idn_punycode_decode(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(idn_punycode_decode);
Datum idn_punycode_decode(PG_FUNCTION_ARGS)
{
    text *arg0;

    char *src, *utf8_dest;
    punycode_uint *ucs4_dest;
    size_t ucs4_len;
    size_t srclen;
    size_t utf8_len;
    text *ret;
    int rc;

    /* before we do anything else */
    if (!check_stringprep()) {
        /* actually, check_stringprep raises ERROR */
        PG_RETURN_NULL();
    }

    if (PG_NARGS() != 1) {
        elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }
    /* while the function is defined as strict, this belts-and-suspenders
     * doesn't hurt
     */
    if (PG_ARGISNULL(0)) {
        PG_RETURN_NULL();
    }
    arg0 = PG_GETARG_TEXT_PP(0);

    src = VARDATA(arg0);
    srclen = VARSIZE(arg0) - VARHDRSZ;

    /* scan argument for values outside of the ascii range */
    if (!ascii_check((uint8_t *) src, srclen)) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Non-ASCII data sent to idn_punycode_decode.")));
        PG_RETURN_NULL();
    }

    /* need to allocate space for decoded data, which will
     * never take more space than the input.
     */
    ucs4_len = srclen;
    /* allocate an array of punycode_uint */
    ucs4_dest = palloc(sizeof(punycode_uint) * ucs4_len);

    rc = punycode_decode(srclen, src, &ucs4_len, ucs4_dest, NULL);

    /* check rc, etc. */
    if (rc != PUNYCODE_SUCCESS) {
        pfree(ucs4_dest);
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error encountered converting from punycode: %s",
                                 punycode_strerror(rc))));
        PG_RETURN_NULL();
    }

    /* now we have to use stringprep to convert from ucs4 to UTF-8 */
    utf8_dest = stringprep_ucs4_to_utf8(ucs4_dest, ucs4_len, NULL, &utf8_len);
    /* utf8_len does not include the trailing NUL byte */

    /* we're done with ucs4_dest */
    pfree(ucs4_dest);

    /* check result of ucs4 conversion */
    if (!utf8_dest) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Unknown error converting from UCS4 to UTF-8.")));
        PG_RETURN_NULL();
    }

    /* convert return value back to whatever the db encoding is */
    ret = utf8_to_text(utf8_dest, utf8_len);

    /* we aren't using utf8_dest */
    free(utf8_dest);

    /* done */
    PG_RETURN_TEXT_P(ret);
}


static text *idn_func_wrapper(utf8_fn func, text *arg0, int32 arg1, bool uses_len);

/* a little bit obnoxious.
 * we (may) have to convert from database encoding to UTF8 for some
 * or 'unicode' (how is this represented?) for others
 *
 * For DB->UTF8, this may be a no-op
 */
static text *idn_func_wrapper(utf8_fn func, text *arg0, int32 arg1, bool uses_len)
{
    char *utf8_src, *res;
    size_t utf8_srclen;
    text *ret;
    bool needs_free;

    /* before we do anything else */
    if (!check_stringprep()) {
        /* actually, check_stringprep raises ERROR */
        return NULL;
    }

    utf8_src = text_to_utf8(arg0, &utf8_srclen, &needs_free, true);

    /* call function */
    res = func(utf8_src, utf8_srclen, arg1);

    if (needs_free) {
        pfree(utf8_src);
    }

    if (res == NULL) {
        return NULL;
    }

    /* convert return value back to whatever the db encoding is */
    ret = utf8_to_text(res, strlen(res));

    /* we aren't using res */
    free(res);

    /* done */
    return (text *) ret;
}


static char *stringprep_utf8_nfkc_normalize_wrapper(const char *src, ssize_t srclen, int32 flags)
{
    /* flags ignored */
    return stringprep_utf8_nfkc_normalize(src, srclen);
}

Datum idn_utf8_nfkc_normalize(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(idn_utf8_nfkc_normalize);
Datum idn_utf8_nfkc_normalize(PG_FUNCTION_ARGS)
{
    text *arg0;
    text *result;
    int32 arg1 = 0;

    if (PG_NARGS() != 1) {
        elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }
    /* while the function is defined as strict, this belts-and-suspenders
     * doesn't hurt
     */
    if (PG_ARGISNULL(0)) {
        PG_RETURN_NULL();
    }
    arg0 = PG_GETARG_TEXT_PP(0);
    /* this function sends a dummy arg1 */

    result = idn_func_wrapper(stringprep_utf8_nfkc_normalize_wrapper, arg0, arg1, true);
    /* NOTE: the libidn documentation does not show that
     * errors are possible with stringprep_utf8_nfkc_normalize.
     */
    if (result == NULL) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Unknown error applying NFKC normalization.")));
        PG_RETURN_NULL();
    }
    PG_RETURN_TEXT_P(result);
}

static char *idna_to_unicode_8z8z_wrapper(const char *src, ssize_t srclen, int32 flags);

/* DO NOT call this with a non NUL-terminated string */
static char *idna_to_unicode_8z8z_wrapper(const char *src, ssize_t srclen, int32 flags)
{
    /* given a UTF-8 encoded string, convert from idna to UTF-8-encoded unicode */
    int retval;
    char *output;

    retval = idna_to_unicode_8z8z(src, &output, flags);

    if (retval == IDNA_SUCCESS) {
        /* sweet */
        return output; /* newly-allocated, NUL-terminated result */
    }

    ereport(WARNING,
            (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
             errmsg_internal("Error encountered converting from IDNA2003 to Unicode: %s", idna_strerror(retval))));
    return NULL; /* error */
}


Datum idn_idna_decode(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(idn_idna_decode);
Datum idn_idna_decode(PG_FUNCTION_ARGS)
{
    text *arg0;
    text *result;
    int32 flags = 0; // = IDNA_USE_STD3_ASCII_RULES; /* or possibly IDNA_ALLOW_UNASSIGNED, etc... */

    switch (PG_NARGS()) {
        case 2:
            if (!PG_ARGISNULL(1)) {
                flags = parse_text_arg_flags(PG_GETARG_TEXT_PP(1), SCOPE_IDNA);
            }
        case 1:
            if (PG_ARGISNULL(0)) {
                PG_RETURN_NULL();
            }
            arg0 = PG_GETARG_TEXT_PP(0);
            break;
        default:
            elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }

    result = idn_func_wrapper(idna_to_unicode_8z8z_wrapper, arg0, flags, false);

    if (result == NULL) {
        PG_RETURN_NULL();
    }
    PG_RETURN_TEXT_P(result);
}

static char *idna_to_ascii_8z_wrapper(const char *src, ssize_t srclen, int32 flags);

/* DO NOT call this with a non-NUL-terminated string */
/* given a UTF-8 encoded string, convert from idna to ASCII */
static char *idna_to_ascii_8z_wrapper(const char *src, ssize_t srclen, int32 flags)
{
    int retval;
    char *output;

    retval = idna_to_ascii_8z(src, &output, flags);

    if (retval == IDNA_SUCCESS) {
        /* sweet */
        return output; /* newly-allocated, NUL-terminated result */
    }

    ereport(WARNING,
            (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
             errmsg_internal("Error encountered converting from IDNA2003 to ASCII: %s", idna_strerror(retval))));
    return NULL; /* error */
}



Datum idn_idna_encode(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(idn_idna_encode);
Datum idn_idna_encode(PG_FUNCTION_ARGS)
{
    text *arg0;
    text *result;
    int32 flags = 0;

    switch (PG_NARGS()) {
        case 2:
            if (!PG_ARGISNULL(1)) {
                flags = parse_text_arg_flags(PG_GETARG_TEXT_PP(1), SCOPE_IDNA);
            }
        case 1:
            if (PG_ARGISNULL(0)) {
                PG_RETURN_NULL();
            }
            arg0 = PG_GETARG_TEXT_PP(0);
            break;
        default:
            elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }

    result = idn_func_wrapper(idna_to_ascii_8z_wrapper, arg0, flags, false);

    if (result == NULL) {
        PG_RETURN_NULL();
    }
    PG_RETURN_TEXT_P(result);
}

Datum idn_pr29_check(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(idn_pr29_check);
Datum idn_pr29_check(PG_FUNCTION_ARGS)
{
    text *arg0;
    char *utf8_src;
    size_t utf8_srclen;
    bool needs_free;
    int ret;

    if (PG_NARGS() != 1) {
        elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }
    /* while the function is defined as strict, this belts-and-suspenders
     * doesn't hurt
     */
    if (PG_ARGISNULL(0)) {
        PG_RETURN_NULL();
    }
    arg0 = PG_GETARG_TEXT_PP(0);

    /* pr28_8z requires a NUL-terminated input */
    utf8_src = text_to_utf8(arg0, &utf8_srclen, &needs_free, true);

    ret = pr29_8z((const char *) utf8_src);

    if (needs_free) {
        pfree(utf8_src);
    }

    if (ret != PR29_SUCCESS && ret != PR29_PROBLEM) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error encountered performing PR29 check: %s", pr29_strerror(ret))));
    }
    PG_RETURN_BOOL(ret == PR29_SUCCESS);
}

Datum libidn2_lookup(PG_FUNCTION_ARGS);
/*
Perform IDNA2008 lookup string conversion on domain name src, as described in section 5 of RFC 5891.
Note that the input string must be encoded in UTF-8.
 */
PG_FUNCTION_INFO_V1(libidn2_lookup);
Datum libidn2_lookup(PG_FUNCTION_ARGS)
{
    text *arg0;
    text *result;
    int flags = 0;
    uint8_t *utf8_src;
    size_t utf8_srclen;
    bool needs_free;
    uint8_t *lookupname;
    int rc;

    switch (PG_NARGS()) {
        case 2:
            if (!PG_ARGISNULL(1)) {
                flags = parse_text_arg_flags(PG_GETARG_TEXT_PP(1), SCOPE_IDNA2);
            }
        case 1:
            if (PG_ARGISNULL(0)) {
                PG_RETURN_NULL();
            }
            arg0 = PG_GETARG_TEXT_PP(0);
            break;
        default:
            elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }

    utf8_src = (uint8_t *) text_to_utf8(arg0, &utf8_srclen, &needs_free, true);

    rc = idn2_lookup_u8(utf8_src, &lookupname, flags);

    if (needs_free) {
        pfree(utf8_src);
    }

    if (rc != IDN2_OK) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error encountered performing idn2 lookup: %s",
                                 idn2_strerror(rc))));
        PG_RETURN_NULL();
    }

    result = utf8_to_text((char *) lookupname, strlen((char *) lookupname));
    free(lookupname);

    PG_RETURN_TEXT_P(result);
}

Datum libidn2_register(PG_FUNCTION_ARGS);
/*
Perform IDNA2008 register string conversion on domain label ulabel and alabel, as described in section 4 of RFC 5891.
Note that the input string must be encoded in UTF-8.
 */
PG_FUNCTION_INFO_V1(libidn2_register);
Datum libidn2_register(PG_FUNCTION_ARGS)
{
    uint8_t
    *ulabel_utf8_src,
    *alabel_ascii_src = NULL;
    size_t
    ulabel_utf8_srclen,
    alabel_ascii_srclen;
    int32 flags = 0;
    bool ulabel_needs_free;
    uint8_t *insertname;
    int rc;
    text *result;

    /* FIXME: for now, use a fixed 3-arg form */
    if (PG_NARGS() != 3) {
        elog(ERROR, "unexpected number of arguments: %d", PG_NARGS());
    }

    if (PG_ARGISNULL(0)) {
        ulabel_utf8_src = NULL;
        ulabel_utf8_srclen = 0;
    } else {
        ulabel_utf8_src = (uint8_t *) text_to_utf8(PG_GETARG_TEXT_PP(0),
                          &ulabel_utf8_srclen,
                          &ulabel_needs_free,
                          true);
    }

    if (PG_ARGISNULL(1)) {
        alabel_ascii_src = NULL;
        alabel_ascii_srclen = 0;
    } else {
        /* the label is supposed to be *ASCII*.
         * Let's check it.
         */
        text *arg1 = PG_GETARG_TEXT_PP(1);
        uint8_t *src = (uint8_t *) VARDATA_ANY(arg1);
        alabel_ascii_srclen = VARSIZE_ANY_EXHDR(arg1);
        alabel_ascii_src = palloc(alabel_ascii_srclen + 1);
        memcpy(alabel_ascii_src, src, alabel_ascii_srclen);
        alabel_ascii_src[alabel_ascii_srclen] = '\0';

        if (!ascii_check(alabel_ascii_src, alabel_ascii_srclen)) {
            ereport(WARNING,
                    (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                     errmsg_internal("Non-ASCII data sent to idn_punycode_decode.")));
            PG_RETURN_NULL();
        }
    }

    if (ulabel_utf8_src == NULL && alabel_ascii_src == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Only one of ulabel, alabel may be NULL.")));
        PG_RETURN_NULL();
    }

    if (PG_ARGISNULL(2)) {
        flags = 0;
    } else {
        flags = parse_text_arg_flags(PG_GETARG_TEXT_PP(2), SCOPE_IDNA2);
    }

    rc = idn2_register_u8(ulabel_utf8_src, alabel_ascii_src, &insertname, flags);

    if (ulabel_needs_free) {
        pfree(ulabel_utf8_src);
    }
    if (alabel_ascii_src) {
        pfree(alabel_ascii_src);
    }

    if (rc != IDN2_OK) {
        ereport(WARNING,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION),
                 errmsg_internal("Error encountered performing idn2 register: %s",
                                 idn2_strerror(rc))));
        PG_RETURN_NULL();
    }

    result = utf8_to_text((char *) insertname, strlen((char *) insertname));
    free(insertname);

    PG_RETURN_TEXT_P(result);
}

Datum idn_constants(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(idn_constants);
Datum idn_constants(PG_FUNCTION_ARGS)
{
    FuncCallContext  *funcctx;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext oldcontext;
        TupleDesc       tupdesc;

        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        tupdesc = CreateTemplateTupleDesc(3, false);
        TupleDescInitEntry(tupdesc, (AttrNumber) 1, "name",
                           TEXTOID, -1, 0);
        TupleDescInitEntry(tupdesc, (AttrNumber) 2, "value",
                           INT4OID, -1, 0);
        TupleDescInitEntry(tupdesc, (AttrNumber) 3, "description",
                           TEXTOID, -1, 0);

        funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);
        funcctx->max_calls = sizeof(_constants) / sizeof(struct idn_constants_struct);

        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();

    if (funcctx->call_cntr < funcctx->max_calls) {
        char **values = NULL;
        HeapTuple tuple;
        Datum result;
        int i;

        i = funcctx->call_cntr;

        values = (char **) palloc(3 * sizeof(char *));
        values[0] = pstrdup(_constants[i].name);;
        values[1] = (char *) palloc(12);       /* sign, 10 digits, '\0' */
        pg_itoa(_constants[i].value, values[1]);
        values[2] = pstrdup(_constants[i].description);;

        tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
        result = HeapTupleGetDatum(tuple);

        for (i = 0; i < 3; i++) {
            pfree(values[i]);
        }
        pfree(values);
        /* don't increase funcctx->call_cntr, PG does that for us */
        SRF_RETURN_NEXT(funcctx, result);
    }
    SRF_RETURN_DONE(funcctx);
}

