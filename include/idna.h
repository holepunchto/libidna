#ifndef IDNA_H
#define IDNA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <utf.h>
#include <utf/string.h>

/**
 * Converts a domain to its ASCII form as a URL parser needs it, appending it to
 * `result`.
 *
 * This is Unicode ToASCII with the flags that a URL parser calls for, being
 * CheckBidi and CheckJoiners set to true and UseSTD3ASCIIRules, CheckHyphens,
 * VerifyDnsLength, Transitional_Processing, and IgnoreInvalidPunycode all set
 * to false.
 *
 * A domain for which UTS #46 records an error is of no use to a URL parser, so
 * such a domain is rejected by returning -1 rather than converted as far as is
 * possible.
 *
 * Returns 0 on success and -1 if the domain cannot be converted, which covers a
 * domain that is not well-formed UTF-8, a label that fails the validity
 * criteria, a label that is not valid Punycode, and a Bidi domain name with a
 * label that breaks the Bidi rule.
 *
 * https://www.unicode.org/reports/tr46/#ToASCII
 */
int
idna_url_to_ascii(utf8_string_view_t domain, utf8_string_t *result);

#ifdef __cplusplus
}
#endif

#endif // IDNA_H
