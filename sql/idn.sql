

set bytea_output = 'escape';

CREATE EXTENSION idn;

-- pr29 tests
select idn_pr29_check(u&'d\0061t\+000061'::text);
-- http://www.unicode.org/review/pr-29.html

select idn_pr29_check(u&'d\0B47\0300\0B3E'::text);
select idn_pr29_check(u&'d\1100\0300\1161'::text);
-- from a libidn bug report
select idn_pr29_check(u&'d\1100\0300\4711'::text);

-- idna encoding/decoding
-- smoke tests
select idn_idna_decode('foo.bar.baz');
select idn_idna_encode('foo.bar.baz');

-- from http://www.unicode.org/faq/idn.html
select idn_idna_decode('xn--bcher-kva.de');
select idn_idna_encode('bücher.de');
select idn_idna_encode(u&'b\0075\0308cher.de');
select idn_idna_decode('xn--bcher-kva.de', 'IDNA_FLAG_NONE');
select idn_idna_encode('bücher.de', 'IDNA_FLAG_NONE');
select idn_idna_encode(u&'b\0075\0308cher.de', 'IDNA_FLAG_NONE');

-- check IDNA_USE_STD3_ASCII_RULES
-- (basically, 2003 disallowed all but a-z, 0-9, and dash.)
select idn_idna_encode('foo.ba_r.baz', 'IDNA_FLAG_NONE'); -- success
select idn_idna_encode('foo.ba_r.baz', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail
select idn_idna_encode('foo.ba_r.ba--z', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail
-- check the not-equal sign
select idn_idna_encode(u&'foo.bar.\2260baz', 'IDNA_FLAG_NONE'); -- success
select idn_idna_encode(u&'foo.bar.\2260baz', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail
-- check leading minus
select idn_idna_encode('-foo.bar.baz', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail
-- check trailing minus
select idn_idna_encode('foo.bar.baz-', 'IDNA_FLAG_USE_STD3_ASCII_RULES'); -- fail

-- check length
select idn_idna_encode('third.label.is.morethan63characterslongasyoucanseethisisaverylonglabeltoolonginfact'); -- fail

-- punycode
select idn_punycode_encode('bücher.de');
select idn_punycode_encode(u&'b\0075\0308cher.de');
select idn_punycode_decode('bcher.de-65a');

-- LATIN SMALL LETTER E WITH ACUTE
-- first, in NFD
select idn_punycode_encode(u&'\0065\0301') = 'e-xbb';
-- then in NFKC
select idn_punycode_encode(idn_utf8_nfkc_normalize(u&'\0065\0301')) = '9ca';

-- should return true:
select idn_utf8_nfkc_normalize(u&'\0065\0301') = u&'\00e9';
select idn_utf8_nfkc_normalize(u&'\2079') = '9';

-- idna 2003 vs 2008 bits
-- from http://www.unicode.org/faq/idn.html
-- LATIN SMALL LETTER SHARP S
select idn_idna_encode(u&'\00DF') = 'ss'; -- 2003
select idn2_lookup(u&'\00DF') = 'xn--zca'; -- 2008
select idn2_register(u&'\00DF', NULL, 'IDN2_FLAG_NONE') = 'xn--zca'; -- 2008

-- U+03C2 GREEK SMALL LETTER FINAL SIGMA
-- U+200C ZERO WIDTH NON-JOINER
-- U+200D ZERO WIDTH JOINER

-- (öbb.at)
-- should be same in 2003/2008/UTS46
select idn_idna_encode(u&'\00f6bb.at') = 'xn--bb-eka.at';
select idn2_lookup(u&'\00f6bb.at') = 'xn--bb-eka.at';
-- FIXME: libidn2 says this is disallowed for registration.
-- AFAICT, the Unicode FAQ page doesn't say anything about
-- it one way or the other.
select idn2_register(u&'\00f6bb.at', NULL, 'IDN2_FLAG_NONE'); -- disallowed?


-- (ÖBB.at)
-- 2003 allows (w/case change)
-- UTS46 allows (w/case change)
-- 2008 disallows
select idn_idna_encode(u&'\00d6BB.at') = 'xn--bb-eka.at';
select idn2_lookup(u&'\00d6BB.at'); -- should disallow
select idn2_register(u&'\00d6BB.at', NULL, 'IDN2_FLAG_NONE'); -- should disallow

-- (√.com)
-- 2003 + UTS46 allow, 2008 disallows
select idn_idna_encode(u&'\221a.com') = 'xn--19g.com';
select idn2_lookup(u&'\221a.com'); -- should disallow
select idn2_register(u&'\221a.com', NULL, 'IDN2_FLAG_NONE'); -- should disallow

-- (faß.de)
-- valid across all three, but difference answers for 2008
select idn_idna_encode(u&'fa\00df.de') = 'fass.de'; -- 2003
select idn2_lookup(u&'fa\00df.de') = 'xn--fa-hia.de'; -- 2008
-- FIXME: libidn2 says this is disallowed for registration.
-- AFAICT, the Unicode FAQ page doesn't say anything about
-- it one way or the other.
select idn2_register(u&'fa\00df.de', NULL, 'IDN2_FLAG_NONE'); -- disallowed for reg?
-- FIXME: unknown what UTS46 does

-- (ԛәлп.com)
-- valid for 2003 lookup, but not registration
-- valid for 2008 + UTS46
select idn_idna_encode(u&'\051b\04d9\043b\043f.com'); -- should fail (2003)
select idn_idna_encode(u&'\051b\04d9\043b\043f.com', 'IDNA_FLAG_ALLOW_UNASSIGNED') = 'xn--k1ai47bhi.com'; -- 2003
select idn2_lookup(u&'\051b\04d9\043b\043f.com') = 'xn--k1ai47bhi.com'; -- 2008
-- FIXME: libidn2 says this is disallowed for registration.
-- AFAICT, the Unicode FAQ page doesn't say anything about
-- it one way or the other.
select idn2_register(u&'\051b\04d9\043b\043f.com', NULL, 'IDN2_FLAG_NONE'); -- disallowed for 2008 reg?

-- (Ⱥbby.com)
-- valid for 2003 lookup, not registration
-- valid for UTS46 (w/case change)
-- NOT valid for 2008
select idn_idna_encode(u&'\023abby.com'); -- should fail
select idn_idna_encode(u&'\023abby.com', 'IDNA_FLAG_ALLOW_UNASSIGNED') = 'xn--bby-spb.com'; -- 2003
select idn2_lookup(u&'\023abby.com', 'IDN2_FLAG_NONE'); -- 2008, fails
select idn2_register(u&'\023abby.com', NULL, 'IDN2_FLAG_NONE'); -- 2008, fails


-- check round trip
select idn2_register(u&'\00DF'::text, 'xn--zca', 'IDN2_FLAG_ALABEL_ROUNDTRIP'::text);
select idn2_register(NULL, 'xn--zca', 'IDN2_FLAG_ALABEL_ROUNDTRIP'::text);
select idn2_register(NULL, 'xn--zca', NULL);

-- check NFC IDN2_NFC_INPUT
-- the following are in NFD
select idn2_lookup('regcombéídn.example'); --- fails
select idn2_lookup(u&'regcombe\0301i\0301dn.example'); --- fails
select idn2_lookup('regcombéídn.example', 'IDN2_FLAG_NFC_INPUT'); -- succeeds
select idn2_lookup(u&'regcombe\0301i\0301dn.example', 'IDN2_FLAG_NFC_INPUT'); -- succeeds
-- the following *may* succeed, but libidn2 currently has IDN2_FLAG_ALABEL_ROUNDTRIP
-- for lookup listed as a FIXME
-- select idn2_lookup('xn--regcombdn-h4a8b.example', 'IDN2_FLAG_NFC_INPUT|IDN2_FLAG_ALABEL_ROUNDTRIP');
-- select idn2_lookup('xn--regcombdn-h4a8b.example', 'IDN2_FLAG_ALABEL_ROUNDTRIP|IDN2_FLAG_NFC_INPUT');

-- stringprep bits
select stringprep(E'foo.bar.baz', 'trace');
select stringprep(E'foo.bar.baz', 'trace', 'STRINGPREP_FLAG_NONE');
select stringprep(E'foo\003.bar.baz', 'trace'); -- fail

-- TODO
-- UTS46 tests
