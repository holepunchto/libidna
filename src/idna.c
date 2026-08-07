#include <idna/tables.h>
#include <normalize.h>
#include <punycode.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <utf.h>
#include <utf/string.h>

#include "../include/idna.h"

/**
 * The first code point with Bidi_Class R, AL, or AN, counting the classes that
 * unassigned code points default to.
 */
#define IDNA_FIRST_RTL 0x590

/**
 * Looks up the value that a sorted, gap-free list of ranges gives a code point,
 * which is the value of the last range that starts at or below it. The search is
 * narrowed down to the block that the code point belongs to.
 */
static inline uint8_t
idna__range_value(const idna__range_t *ranges, const uint16_t *blocks, utf32_t c) {
  size_t block = c >> IDNA_BLOCK_SHIFT;

  size_t lo = blocks[block], hi = blocks[block + 1];

  while (lo < hi) {
    size_t mid = lo + (hi - lo + 1) / 2;

    if (ranges[mid] >> 8 <= c) lo = mid;
    else hi = mid - 1;
  }

  return ranges[lo] & 0xff;
}

static inline uint8_t
idna__properties(utf32_t c) {
  return idna__range_value(idna__property_ranges, idna__property_blocks, c);
}

// https://www.unicode.org/reports/tr44/#Bidi_Class
static inline idna__bidi_class_t
idna__bidi_class(utf32_t c) {
  return (idna__bidi_class_t) (idna__properties(c) & 0xf);
}

// https://www.unicode.org/reports/tr44/#Joining_Type
static inline idna__joining_type_t
idna__joining_type(utf32_t c) {
  return (idna__joining_type_t) ((idna__properties(c) >> 4) & 0x7);
}

// https://www.unicode.org/reports/tr44/#General_Category_Values
static inline bool
idna__is_mark(utf32_t c) {
  // A combining mark is the first thing to have a combining class, so nothing
  // below the first of them can be one.
  if (c < NORMALIZE_FIRST_MARK) return false;

  return (idna__properties(c) >> 7) != 0;
}

/**
 * Looks up the IDNA status of a code point, setting `mapping` and `len` to its
 * mapping if the status is `idna__status_mapped`.
 *
 * https://www.unicode.org/reports/tr46/#IDNA_Mapping_Table
 */
static inline idna__status_t
idna__status(utf32_t c, const utf32_t **mapping, size_t *len) {
  uint32_t value;

  if (c < 0x80) {
    value = idna__ascii_mappings[c];
  } else {
    size_t block = c >> IDNA_BLOCK_SHIFT;

    size_t lo = idna__mapping_blocks[block], hi = idna__mapping_blocks[block + 1];

    while (lo < hi) {
      size_t mid = lo + (hi - lo + 1) / 2;

      if (idna__mappings[mid].start <= c) lo = mid;
      else hi = mid - 1;
    }

    value = idna__mappings[lo].mapping;
  }

  *mapping = &idna__mapping_data[value >> 7];
  *len = (value >> 2) & 0x1f;

  return (idna__status_t) (value & 0x3);
}

/**
 * The prefix that marks a label as being Punycode encoded.
 *
 * https://www.rfc-editor.org/rfc/rfc3490#section-5
 */
#define IDNA_ACE_PREFIX "xn--"

#define IDNA_ACE_PREFIX_LEN 4

static inline bool
idna__is_ascii(const utf32_t *code_points, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (code_points[i] > 0x7f) return false;
  }

  return true;
}

static inline bool
idna__has_ace_prefix(const utf32_t *label, size_t len) {
  return len >= IDNA_ACE_PREFIX_LEN && label[0] == 'x' && label[1] == 'n' && label[2] == '-' && label[3] == '-';
}

/**
 * Checks whether a code point is a virama, being one of the combining marks that
 * either joiner may follow.
 *
 * https://www.rfc-editor.org/rfc/rfc5892#appendix-A.1
 */
static inline bool
idna__is_virama(utf32_t c) {
  return normalize_combining_class(c) == 9;
}

/**
 * Appends the full canonical decomposition of `c` to `result`.
 *
 * The decomposer is given the most that a single code point can decompose to, and
 * writes into the tail of `result` directly. Taking it by way of a buffer of that
 * size instead would grow `result` by only what the decomposition turned out to
 * need, but costs a copy of it for every code point.
 */
static inline int
idna__decompose(utf32_t c, utf32_string_t *result) {
  int err;

  err = utf32_string_reserve(result, result->len + NORMALIZE_MAX_DECOMPOSITION);
  if (err < 0) return err;

  result->len += normalize_decompose(c, &result->data[result->len]);

  return 0;
}

/**
 * Checks a label against the ContextJ rules, which restrict the contexts in
 * which the zero width non-joiner and joiner may appear.
 *
 * https://www.rfc-editor.org/rfc/rfc5892#appendix-A.1
 * https://www.rfc-editor.org/rfc/rfc5892#appendix-A.2
 */
static inline bool
idna__check_joiners(const utf32_t *label, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (label[i] != 0x200c && label[i] != 0x200d) continue;

    // Either joiner may follow a virama, where it controls whether the two
    // characters it sits between take a conjunct form.
    if (i > 0 && idna__is_virama(label[i - 1])) continue;

    // The zero width joiner has no other context in which it is allowed.
    if (label[i] == 0x200d) return false;

    // The zero width non-joiner may also keep apart two characters that would
    // otherwise join, which is the case if it is preceded by a left or dual
    // joining character and followed by a right or dual joining character,
    // disregarding any transparent characters in between.
    size_t before = i;

    while (before > 0 && idna__joining_type(label[before - 1]) == idna__joining_type_transparent) {
      before--;
    }

    if (before == 0) return false;

    idna__joining_type_t type = idna__joining_type(label[before - 1]);

    if (type != idna__joining_type_left && type != idna__joining_type_dual) return false;

    size_t after = i + 1;

    while (after < len && idna__joining_type(label[after]) == idna__joining_type_transparent) {
      after++;
    }

    if (after == len) return false;

    type = idna__joining_type(label[after]);

    if (type != idna__joining_type_right && type != idna__joining_type_dual) return false;
  }

  return true;
}

/**
 * Checks a label of a Bidi domain name against the six numbered conditions of
 * the Bidi rule.
 *
 * https://www.rfc-editor.org/rfc/rfc5893#section-2
 */
static inline bool
idna__check_bidi(const utf32_t *label, size_t len) {
  if (len == 0) return true;

  bool rtl;

  // 1. The first character must be a character with Bidi property L, R, or AL.
  //    If it has the R or AL property, it is an RTL label; if it has the L
  //    property, it is an LTR label.
  switch (idna__bidi_class(label[0])) {
  case idna__bidi_class_r:
  case idna__bidi_class_al:
    rtl = true;
    break;

  case idna__bidi_class_l:
    rtl = false;
    break;

  default:
    return false;
  }

  bool european_number = false, arabic_number = false;

  // The last character that is not a nonspacing mark, those being disregarded
  // by the conditions on the end of the label.
  size_t end = 0;

  for (size_t i = 0; i < len; i++) {
    idna__bidi_class_t bidi_class = idna__bidi_class(label[i]);

    if (rtl) {
      // 2. In an RTL label, only characters with the Bidi properties R, AL, AN,
      //    EN, ES, CS, ET, ON, BN, or NSM are allowed.
      switch (bidi_class) {
      case idna__bidi_class_r:
      case idna__bidi_class_al:
      case idna__bidi_class_an:
      case idna__bidi_class_en:
      case idna__bidi_class_es:
      case idna__bidi_class_cs:
      case idna__bidi_class_et:
      case idna__bidi_class_on:
      case idna__bidi_class_bn:
      case idna__bidi_class_nsm:
        break;

      default:
        return false;
      }
    } else {
      // 5. In an LTR label, only characters with the Bidi properties L, EN, ES,
      //    CS, ET, ON, BN, or NSM are allowed.
      switch (bidi_class) {
      case idna__bidi_class_l:
      case idna__bidi_class_en:
      case idna__bidi_class_es:
      case idna__bidi_class_cs:
      case idna__bidi_class_et:
      case idna__bidi_class_on:
      case idna__bidi_class_bn:
      case idna__bidi_class_nsm:
        break;

      default:
        return false;
      }
    }

    if (bidi_class == idna__bidi_class_en) european_number = true;
    else if (bidi_class == idna__bidi_class_an) arabic_number = true;

    if (bidi_class != idna__bidi_class_nsm) end = i;
  }

  if (rtl) {
    // 3. In an RTL label, the end of the label must be a character with Bidi
    //    property R, AL, EN, or AN, followed by zero or more characters with
    //    Bidi property NSM.
    switch (idna__bidi_class(label[end])) {
    case idna__bidi_class_r:
    case idna__bidi_class_al:
    case idna__bidi_class_en:
    case idna__bidi_class_an:
      break;

    default:
      return false;
    }

    // 4. In an RTL label, if an EN is present, no AN may be present, and vice
    //    versa.
    if (european_number && arabic_number) return false;
  } else {
    // 6. In an LTR label, the end of the label must be a character with Bidi
    //    property L or EN, followed by zero or more characters with Bidi
    //    property NSM.
    switch (idna__bidi_class(label[end])) {
    case idna__bidi_class_l:
    case idna__bidi_class_en:
      break;

    default:
      return false;
    }
  }

  return true;
}

/**
 * Checks a label against the validity criteria, apart from the two that the
 * conversion of the domain checks itself.
 *
 * https://www.unicode.org/reports/tr46/#Validity_Criteria
 */
static inline bool
idna__is_valid_label(const utf32_t *label, size_t len) {
  // Only a non-empty label is subject to the validity criteria.
  if (len == 0) return true;

  // 1. The label must be in Unicode Normalization Form NFC. Only a label that
  //    was converted from Punycode can fail this, the domain having been
  //    normalized as a whole before being broken into labels, and so it is
  //    checked as part of that conversion.

  // 2. and 3. are skipped as CheckHyphens is false.

  // 4. If not CheckHyphens, the label must not begin with "xn--".
  if (idna__has_ace_prefix(label, len)) return false;

  // 5. The label must not contain a U+002E ( . ) FULL STOP. The domain is
  //    broken into labels at every full stop, so this holds by construction.

  // 6. The label must not begin with a combining mark, that is:
  //    General_Category=Mark.
  if (idna__is_mark(label[0])) return false;

  // 7. Each code point in the label must only have certain status values
  //    according to the IDNA mapping table, which for nontransitional
  //    processing means either valid or deviation.
  for (size_t i = 0; i < len; i++) {
    const utf32_t *mapping;
    size_t mapping_len;

    if (idna__status(label[i], &mapping, &mapping_len) != idna__status_valid) return false;
  }

  // 8. If CheckJoiners, the label must satisfy the ContextJ rules.
  if (!idna__check_joiners(label, len)) return false;

  // 9. The Bidi rule is checked once the domain as a whole is known to be a Bidi
  //    domain name.

  return true;
}

int
idna_url_to_ascii(utf8_string_view_t input, utf8_string_t *result) {
  int err;

  utf32_string_t decoded, normalized, converted, label;

  utf32_string_init(&decoded);
  utf32_string_init(&normalized);
  utf32_string_init(&converted);
  utf32_string_init(&label);

  bool bidi = false;

  err = utf32_string_reserve(&decoded, utf32_length_from_utf8(input.data, input.len));
  if (err < 0) goto err;

  // The decoder both validates and converts in a single pass, stopping short
  // and reporting a length of zero the moment it meets an ill-formed byte
  // sequence. An empty input decodes to nothing without being ill-formed, so
  // only a length of zero from a non-empty input is a rejection. Validating
  // the input separately beforehand would be a wasted pass over it, and a
  // domain that carries a genuine U+FFFD from an earlier lossy decode is still
  // rejected, that code point being disallowed by the validity criteria.
  decoded.len = utf8_convert_to_utf32(input.data, input.len, decoded.data);

  if (decoded.len == 0 && input.len != 0) goto err;

  // Mapping and decomposing rarely lengthen a domain, so the normalized domain
  // tends to end up close to the decoded one in size. Reserving that much up
  // front grows the buffer in one step for a domain that neither maps to nor
  // decomposes into more code points than it started with, rather than letting
  // it double its way there a code point at a time. A domain that does lengthen
  // grows the buffer the rest of the way as it goes.
  err = utf32_string_reserve(&normalized, decoded.len);
  if (err < 0) goto err;

  // 1. Map each code point according to its status in the IDNA mapping table and
  //    2. normalize the domain to Unicode Normalization Form C, decomposing as
  //    the mapping goes so that the two steps take a single pass.
  for (size_t i = 0; i < decoded.len; i++) {
    const utf32_t *mapping;
    size_t mapping_len;

    switch (idna__status(decoded.data[i], &mapping, &mapping_len)) {
    case idna__status_mapped:
      for (size_t j = 0; j < mapping_len; j++) {
        err = idna__decompose(mapping[j], &normalized);
        if (err < 0) goto err;
      }
      break;

    case idna__status_ignored:
      break;

    // A disallowed code point is left in place for the validity criteria to
    // reject once the domain has been mapped and normalized.
    case idna__status_valid:
    case idna__status_disallowed:
    default:
      err = idna__decompose(decoded.data[i], &normalized);
      if (err < 0) goto err;
      break;
    }
  }

  // A domain of nothing but ASCII is already in Normalization Form C: no ASCII
  // code point has a canonical decomposition, a nonzero combining class, or a
  // composition with any other, so mapping leaves it as it stands and there is
  // nothing for recomposition to reorder or combine. Every byte of an all-ASCII
  // input decodes to a single code point, so the decoded length matching the
  // input length is exactly the condition under which recomposition is a no-op
  // and may be skipped, which also spares the whole-domain pass a Punycode
  // label makes over ASCII that only decodes to Unicode later, label by label.
  if (decoded.len != input.len) {
    normalized.len = normalize_recompose(normalized.data, normalized.len);
  }

  // The converted domain differs from the normalized one only in its Punycode
  // labels, which are decoded back to Unicode. A domain with no such label is
  // therefore already its own converted form, and every label is checked
  // against the validity criteria where it stands, without a second buffer.
  const utf32_t *domain;
  size_t domain_len;

  // 3. Break the domain into labels at U+002E ( . ) FULL STOP and verify that
  //    each label meets the validity criteria. A Punycode label cannot be
  //    validated until it has been decoded, so meeting one sends the whole
  //    domain down the rebuild path below.
  for (size_t i = 0, start = 0; i <= normalized.len; i++) {
    if (i != normalized.len && normalized.data[i] != '.') continue;

    if (idna__has_ace_prefix(&normalized.data[start], i - start)) goto rebuild;

    if (!idna__is_valid_label(&normalized.data[start], i - start)) goto err;

    start = i + 1;
  }

  domain = normalized.data;
  domain_len = normalized.len;

  goto validated;

rebuild:
  // A Punycode label decodes to at most as many code points as it was encoded
  // from, so the converted domain is no longer than the normalized one but for
  // what normalizing a decoded label adds, which reserves its own room. This
  // covers the common case in a single allocation rather than growing the
  // buffer label by label.
  err = utf32_string_reserve(&converted, normalized.len);
  if (err < 0) goto err;

  // 4. Convert every label that is Punycode encoded back to Unicode, verifying
  //    each label against the validity criteria as before.
  for (size_t i = 0, start = 0; i <= normalized.len; i++) {
    if (i != normalized.len && normalized.data[i] != '.') continue;

    if (start > 0) {
      err = utf32_string_append_character(&converted, '.');
      if (err < 0) goto err;
    }

    size_t offset = converted.len;

    if (idna__has_ace_prefix(&normalized.data[start], i - start)) {
      // A Punycode encoded label may not contain a non-ASCII code point.
      if (!idna__is_ascii(&normalized.data[start], i - start)) goto err;

      size_t encoded_len = i - start - IDNA_ACE_PREFIX_LEN;

      utf32_string_clear(&label);

      // The decoder needs room for the most that the label could decode to, being
      // as many code points as it was encoded from. That is more than it usually
      // needs, so a label short enough to be decoded within this buffer is, which
      // keeps the buffer above off the heap for as long as what comes out of the
      // decoder fits in it. A domain name label runs to 63 code points at most,
      // less the prefix that has already been taken off.
      utf32_t buf[64];

      utf32_t *decoded_label = buf;

      if (encoded_len > sizeof(buf) / sizeof(utf32_t)) {
        err = utf32_string_reserve(&label, utf32_max_length_from_punycode(encoded_len));
        if (err < 0) goto err;

        decoded_label = label.data;
      }

      size_t decoded_len;

      // The basic code points of the label are handed to the decoder as they
      // already stand, rather than narrowed to bytes first.
      err = punycode_decode_utf32(
        &normalized.data[start + IDNA_ACE_PREFIX_LEN],
        encoded_len,
        decoded_label,
        &decoded_len
      );
      if (err < 0) goto err;

      if (decoded_label == buf) {
        // Now that the length is known, only that much of the buffer above is
        // taken, which is what keeps a label of a usual length within it.
        err = utf32_string_append_literal(&label, buf, decoded_len);
        if (err < 0) goto err;
      } else {
        label.len = decoded_len;
      }

      // A label that decodes to nothing, or to nothing but ASCII, had no
      // business being Punycode encoded to begin with.
      if (label.len == 0 || idna__is_ascii(label.data, label.len)) goto err;

      // The first of the validity criteria: the label must be in Normalization
      // Form C. Normalizing it is at the same time how it joins the converted
      // domain, the two being one and the same if it was already normalized.
      size_t bound = normalize_max_length(label.len);

      if (bound == (size_t) -1 || bound > SIZE_MAX - converted.len) goto err;

      err = utf32_string_reserve(&converted, converted.len + bound);
      if (err < 0) goto err;

      converted.len += normalize_nfc(label.data, label.len, &converted.data[converted.len]);

      if (converted.len - offset != label.len) goto err;
      if (memcmp(&converted.data[offset], label.data, label.len * sizeof(utf32_t)) != 0) goto err;
    } else {
      err = utf32_string_append_literal(&converted, &normalized.data[start], i - start);
      if (err < 0) goto err;
    }

    if (!idna__is_valid_label(&converted.data[offset], converted.len - offset)) goto err;

    start = i + 1;
  }

  domain = converted.data;
  domain_len = converted.len;

validated:
  // A Bidi domain name is a domain name containing at least one character with
  // Bidi_Class R, AL, or AN.
  for (size_t i = 0; i < domain_len && !bidi; i++) {
    if (domain[i] < IDNA_FIRST_RTL) continue;

    switch (idna__bidi_class(domain[i])) {
    case idna__bidi_class_r:
    case idna__bidi_class_al:
    case idna__bidi_class_an:
      bidi = true;
      break;

    default:
      break;
    }
  }

  // The last of the validity criteria, which only the labels of a Bidi domain
  // name are subject to.
  if (bidi) {
    for (size_t i = 0, start = 0; i <= domain_len; i++) {
      if (i != domain_len && domain[i] != '.') continue;

      if (!idna__check_bidi(&domain[start], i - start)) goto err;

      start = i + 1;
    }
  }

  // A domain of nothing but ASCII is emitted unchanged, one byte per code
  // point, so its result is exactly its own length and is reserved up front to
  // append it in one step rather than a label at a time. A domain with a label
  // to encode is left alone: Punycode reserves room by a far looser bound, and
  // reserving the length here as well would only allocate twice. The test costs
  // a scan of the domain but stops at the first non-ASCII code point, so it is
  // cheap for the very domains it declines to reserve for.
  if (idna__is_ascii(domain, domain_len)) {
    err = utf8_string_reserve(result, result->len + domain_len);
    if (err < 0) goto err;
  }

  // ToASCII step 3: convert every label with non-ASCII characters into Punycode
  // and prefix it by "xn--". Step 4, verifying the DNS length restrictions, is
  // skipped as VerifyDnsLength is false.
  for (size_t i = 0, start = 0; i <= domain_len; i++) {
    if (i != domain_len && domain[i] != '.') continue;

    if (start > 0) {
      err = utf8_string_append_character(result, '.');
      if (err < 0) goto err;
    }

    if (idna__is_ascii(&domain[start], i - start)) {
      // Every code point of an ASCII label is a single byte, so the whole run
      // is appended at once rather than a byte at a time, each of which would
      // otherwise reserve room for and store one byte on its own.
      err = utf8_string_reserve(result, result->len + (i - start));
      if (err < 0) goto err;

      for (size_t j = start; j < i; j++) {
        result->data[result->len++] = (utf8_t) domain[j];
      }
    } else {
      err = utf8_string_append_literal(result, (utf8_t *) IDNA_ACE_PREFIX, IDNA_ACE_PREFIX_LEN);
      if (err < 0) goto err;

      size_t bound = punycode_max_length_from_utf32(i - start);

      if (bound == (size_t) -1 || bound > SIZE_MAX - result->len) goto err;

      // The label is encoded straight into the result, which is grown to the most
      // that encoding it could call for beforehand.
      err = utf8_string_reserve(result, result->len + bound);
      if (err < 0) goto err;

      size_t encoded_len;

      err = punycode_encode_utf8(&domain[start], i - start, &result->data[result->len], &encoded_len);
      if (err < 0) goto err;

      result->len += encoded_len;
    }

    start = i + 1;
  }

  err = 0;

  goto done;

err:
  err = -1;

done:
  utf32_string_destroy(&decoded);
  utf32_string_destroy(&normalized);
  utf32_string_destroy(&converted);
  utf32_string_destroy(&label);

  return err;
}
