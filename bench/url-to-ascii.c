#include <idna.h>
#include <stddef.h>
#include <utf.h>
#include <utf/string.h>

int
main() {
  // A domain with a label to decode and a label to leave alone.
  static const char *domain = "xn--bcher-kva.example";

  size_t len = 21;

  for (size_t i = 0; i < 1000000; i++) {
    utf8_string_t result;
    utf8_string_init(&result);

    idna_url_to_ascii(utf8_string_view_init((const utf8_t *) domain, len), &result);

    utf8_string_destroy(&result);
  }
}
