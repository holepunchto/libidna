#include <assert.h>
#include <idna.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <utf.h>
#include <utf/string.h>

// Converts `domain` and asserts that it comes out as `expected`.
static void
check(const char *domain, const char *expected) {
  utf8_string_t result;
  utf8_string_init(&result);

  int e = idna_url_to_ascii(
    utf8_string_view_init((const utf8_t *) domain, strlen(domain)),
    &result
  );

  printf("%s -> %.*s\n", domain, (int) result.len, result.data);

  assert(e == 0);
  assert(result.len == strlen(expected));
  assert(memcmp(result.data, expected, result.len) == 0);

  utf8_string_destroy(&result);
}

// Asserts that `domain` cannot be converted.
static void
check_invalid(const char *domain) {
  utf8_string_t result;
  utf8_string_init(&result);

  int e = idna_url_to_ascii(
    utf8_string_view_init((const utf8_t *) domain, strlen(domain)),
    &result
  );

  printf("%s -> rejected\n", domain);

  assert(e == -1);

  utf8_string_destroy(&result);
}

int
main() {
  // An ASCII domain is left as it stands.
  check("example.com", "example.com");
  check("", "");

  // A non-ASCII label is encoded and given the prefix.
  check("bücher.example", "xn--bcher-kva.example");
  check("münchen.de", "xn--mnchen-3ya.de");

  // A label that is already encoded is left as it is, which is what makes
  // converting idempotent.
  check("xn--bcher-kva.example", "xn--bcher-kva.example");

  // Mapping lowercases and folds, so a domain converts the same however it was
  // written.
  check("EXAMPLE.COM", "example.com");
  check("BÜCHER.example", "xn--bcher-kva.example");

  // A code point that maps to nothing is dropped. U+00AD is the soft hyphen.
  check("ex­ample.com", "example.com");

  // A domain is normalized before it is broken into labels, so a decomposed label
  // gives the same encoding as a composed one.
  check("bücher.example", "xn--bcher-kva.example");

  // The label separators that mapping turns into a full stop.
  check("example。com", "example.com");

  // A label that decodes to nothing but ASCII had no business being encoded.
  check_invalid("xn--a-ecp.example");

  // A label that decodes to a disallowed code point.
  check_invalid("xn--a.example");

  // A disallowed code point is rejected rather than dropped. U+FFFD is disallowed,
  // which is what lets ill-formed UTF-8 be rejected without decoding it, and U+2FF0
  // is an ideographic description character.
  check_invalid("�.example");
  check_invalid("⿰.example");

  // The Arabic tatweel is valid, so a label of nothing but it converts. IDNA 2008
  // restricts where it may appear, but that is a rule of its own rather than one of
  // the validity criteria that UTS #46 applies here.
  check("ـ.example", "xn--chb.example");

  // Ill-formed UTF-8 is rejected without being decoded, a replacement character
  // being disallowed in any case.
  {
    utf8_string_t result;
    utf8_string_init(&result);

    static const utf8_t ill_formed[] = {0xff, 0xfe, '.', 'c', 'o', 'm'};

    int e = idna_url_to_ascii(utf8_string_view_init(ill_formed, sizeof(ill_formed)), &result);

    printf("ill-formed UTF-8 -> rejected\n");

    assert(e == -1);

    utf8_string_destroy(&result);
  }

  // Converting is idempotent, so converting a result again leaves it unchanged.
  {
    static const char *domains[] = {
      "bücher.münchen.example",
      "日本語.jp",
      "xn--bcher-kva.example",
      "example.com"
    };

    for (size_t i = 0; i < 4; i++) {
      utf8_string_t once, twice;
      utf8_string_init(&once);
      utf8_string_init(&twice);

      assert(idna_url_to_ascii(utf8_string_view_init((const utf8_t *) domains[i], strlen(domains[i])), &once) == 0);
      assert(idna_url_to_ascii(utf8_string_view(&once), &twice) == 0);

      assert(once.len == twice.len);
      assert(memcmp(once.data, twice.data, once.len) == 0);

      // A converted domain is always ASCII.
      assert(ascii_validate(once.data, once.len));

      printf("%s -> %.*s (idempotent)\n", domains[i], (int) once.len, once.data);

      utf8_string_destroy(&twice);
      utf8_string_destroy(&once);
    }
  }
}
