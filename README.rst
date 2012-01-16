###############################################
Welcome to the documentation for postgresql-idn
###############################################

********
Overview
********

At a fundamental level, this extension grants access to a number of
functions provided by the `libidn`_ and `libidn2`_ libraries. Not all
of the functions provided by these libraries are exposed.

Whenever possible, the code will transform the encoding of the data
in the database to UTF-8 before further processing.


************
Installation
************

Installation in postgresql is fairly simple::

    CREATE EXTENSION idn;

********
Examples
********

Many examples taken from http://www.unicode.org/faq/idn.html.

- check a string to see if it trips the `PR29`_ check::

    select idn_pr29_check(u&'d\0061t\+000061'::text);
     idn_pr29_check
    ----------------
     t
    (1 row)


    select idn_pr29_check(u&'d\0B47\0300\0B3E'::text);
     idn_pr29_check
    ----------------
     f
    (1 row)

- idna (2003) encoding and decoding::

    select idn_idna_decode('xn--bcher-kva.de');
     idn_idna_decode
    -----------------
     bücher.de
    (1 row)

    select idn_idna_encode('bücher.de');
     idn_idna_encode
    ------------------
     xn--bcher-kva.de
    (1 row)

    select idn_idna_encode(u&'b\0075\0308cher.de');
     idn_idna_encode
    ------------------
     xn--bcher-kva.de
    (1 row)

    select idn_idna_decode('xn--bcher-kva.de', 'IDNA_FLAG_NONE');
     idn_idna_decode
    -----------------
     bücher.de
    (1 row)

    select idn_idna_encode('bücher.de', 'IDNA_FLAG_NONE');
     idn_idna_encode
    ------------------
     xn--bcher-kva.de
    (1 row)

    select idn_idna_encode(u&'b\0075\0308cher.de', 'IDNA_FLAG_NONE');
     idn_idna_encode
    ------------------
     xn--bcher-kva.de
    (1 row)

- using the IDNA_USE_STD3_ASCII_RULES flag:

  First, an example without the flag::

    select idn_idna_encode('foo.ba_r.baz', 'IDNA_FLAG_NONE');
     idn_idna_encode
    -----------------
     foo.ba_r.baz
    (1 row)

  And now with the flag::

    select idn_idna_encode('foo.ba_r.baz', 'IDNA_FLAG_USE_STD3_ASCII_RULES');
    WARNING:  Error encountered converting from IDNA2003 to ASCII: Non-digit/letter/hyphen in input
     idn_idna_encode
    -----------------

    (1 row)

    select idn_idna_encode('foo.ba_r.ba--z', 'IDNA_FLAG_USE_STD3_ASCII_RULES');
    WARNING:  Error encountered converting from IDNA2003 to ASCII: Non-digit/letter/hyphen in input
     idn_idna_encode
    -----------------

    (1 row)

    -- check the not-equal sign
    select idn_idna_encode(u&'foo.bar.\2260baz', 'IDNA_FLAG_NONE'); -- success
       idn_idna_encode
    ----------------------
     foo.bar.xn--baz-dl2a
    (1 row)

    select idn_idna_encode(u&'foo.bar.\2260baz', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail
       idn_idna_encode
    ----------------------
     foo.bar.xn--baz-dl2a
    (1 row)

- punycode encoding and decoding::

    select idn_punycode_encode('bücher.de');
     idn_punycode_encode
    ---------------------
     bcher.de-65a
    (1 row)

    select idn_punycode_encode(u&'b\0075\0308cher.de');
     idn_punycode_encode
    ---------------------
     bucher.de-hkf
    (1 row)

    select idn_punycode_decode('bcher.de-65a');
     idn_punycode_decode
    ---------------------
     bücher.de
    (1 row)

- check NFKC normalization:

  Start by showing that the decomposed form is not equal to the composed
  form::

    select u&'\0065\0301' = u&'\00e9';
     ?column?
    ----------
     f
    (1 row)

  And now show the NFKC normalization::

    select idn_utf8_nfkc_normalize(u&'\0065\0301') = u&'\00e9';
     ?column?
    ----------
     t
    (1 row)

  Show that superscript '9' gets normalized to just '9'::

    select idn_utf8_nfkc_normalize(u&'\2079') = '9';
     ?column?
    ----------
     t
    (1 row)

Some comparisons between IDNA 2003 and 2008 follow.

- LATIN SMALL LETTER SHARP S encodes differently::

    select idn_idna_encode(u&'\00DF') = 'ss';
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\00DF') = 'xn--zca';
     ?column?
    ----------
     t
    (1 row)


- show register vs. lookup for IDNA 2008::

    select idn2_register(u&'\00DF', NULL, 'IDN2_FLAG_NONE') = 'xn--zca';
     ?column?
    ----------
     t
    (1 row)

- more examples from the FAQ::

    -- U+03C2 GREEK SMALL LETTER FINAL SIGMA
    -- U+200C ZERO WIDTH NON-JOINER
    -- U+200D ZERO WIDTH JOINER
    -- (öbb.at)
    -- should be same in 2003/2008/UTS46
    select idn_idna_encode(u&'\00f6bb.at') = 'xn--bb-eka.at';
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\00f6bb.at') = 'xn--bb-eka.at';
     ?column?
    ----------
     t
    (1 row)

    -- FIXME: libidn2 says this is disallowed for registration.
    -- AFAICT, the Unicode FAQ page doesn't say anything about
    -- it one way or the other.
    select idn2_register(u&'\00f6bb.at', NULL, 'IDN2_FLAG_NONE'); -- disallowed?
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)

    -- (ÖBB.at)
    -- 2003 allows (w/case change)
    -- UTS46 allows (w/case change)
    -- 2008 disallows
    select idn_idna_encode(u&'\00d6BB.at') = 'xn--bb-eka.at';
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\00d6BB.at'); -- should disallow
    WARNING:  Error encountered performing idn2 lookup: string contains a disallowed character
     idn2_lookup
    ----------------

    (1 row)

    select idn2_register(u&'\00d6BB.at', NULL, 'IDN2_FLAG_NONE'); -- should disallow
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)

    -- (√.com)
    -- 2003 + UTS46 allow, 2008 disallows
    select idn_idna_encode(u&'\221a.com') = 'xn--19g.com';
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\221a.com'); -- should disallow
    WARNING:  Error encountered performing idn2 lookup: string contains a disallowed character
     idn2_lookup
    ----------------

    (1 row)

    select idn2_register(u&'\221a.com', NULL, 'IDN2_FLAG_NONE'); -- should disallow
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)

    -- (faß.de)
    -- valid across all three, but difference answers for 2008
    select idn_idna_encode(u&'fa\00df.de') = 'fass.de'; -- 2003
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'fa\00df.de') = 'xn--fa-hia.de'; -- 2008
     ?column?
    ----------
     t
    (1 row)

    -- FIXME: libidn2 says this is disallowed for registration.
    -- AFAICT, the Unicode FAQ page doesn't say anything about
    -- it one way or the other.
    select idn2_register(u&'fa\00df.de', NULL, 'IDN2_FLAG_NONE'); -- disallowed for reg?
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)

    -- FIXME: unknown what UTS46 does
    -- (ԛәлп.com)
    -- valid for 2003 lookup, but not registration
    -- valid for 2008 + UTS46
    select idn_idna_encode(u&'\051b\04d9\043b\043f.com'); -- should fail (2003)
    WARNING:  Error encountered converting from IDNA2003 to ASCII: String preparation failed
     idn_idna_encode
    -----------------

    (1 row)

    select idn_idna_encode(u&'\051b\04d9\043b\043f.com', 'IDNA_FLAG_ALLOW_UNASSIGNED') = 'xn--k1ai47bhi.com'; -- 2003
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\051b\04d9\043b\043f.com') = 'xn--k1ai47bhi.com'; -- 2008
     ?column?
    ----------
     t
    (1 row)

    -- FIXME: libidn2 says this is disallowed for registration.
    -- AFAICT, the Unicode FAQ page doesn't say anything about
    -- it one way or the other.
    select idn2_register(u&'\051b\04d9\043b\043f.com', NULL, 'IDN2_FLAG_NONE'); -- disallowed for 2008 reg?
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)

    -- (Ⱥbby.com)
    -- valid for 2003 lookup, not registration
    -- valid for UTS46 (w/case change)
    -- NOT valid for 2008
    select idn_idna_encode(u&'\023abby.com'); -- should fail
    WARNING:  Error encountered converting from IDNA2003 to ASCII: String preparation failed
     idn_idna_encode
    -----------------

    (1 row)

    select idn_idna_encode(u&'\023abby.com', 'IDNA_FLAG_ALLOW_UNASSIGNED') = 'xn--bby-spb.com'; -- 2003
     ?column?
    ----------
     t
    (1 row)

    select idn2_lookup(u&'\023abby.com', 'IDN2_FLAG_NONE'); -- 2008, fails
    WARNING:  Error encountered performing idn2 lookup: string contains a disallowed character
     idn2_lookup
    ----------------

    (1 row)

    select idn2_register(u&'\023abby.com', NULL, 'IDN2_FLAG_NONE'); -- 2008, fails
    WARNING:  Error encountered performing idn2 register: string contains a disallowed character
     idn2_register
    ------------------

    (1 row)


**********
TODO/NOTES
**********

* The TLD checking code is not exposed. Primarily, this is due
  to the fact that the tables are hard-coded into the library and
  may be out-of-date. With the appropriate warning/caveat noted,
  however, it may be reasonable to include them.
* the punycode functions in libidn expose a facility for case-folding.
  Since PostgreSQL already includes case-folding smarts, the value-add
  wasn't deemed worth the extra complexity cost.


.. FIN

.. _`libidn`: http://www.gnu.org/software/libidn/
.. _`libidn2`: http://www.gnu.org/software/libidn/libidn2/manual/libidn2.html
.. _`pr29`: http://www.unicode.org/review/pr-29.html

