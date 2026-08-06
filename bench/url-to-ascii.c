#include <idna.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <utf.h>
#include <utf/string.h>

// A set of representative domains, chosen to exercise the different paths that
// the converter takes: a label to decode beside one to leave alone, a plain
// non-ASCII label, an already-ASCII domain, a domain that actually needs
// normalizing, a domain of many labels, and a long single label near the 63
// code point limit.
static const struct {
  const char *name;
  const char *domain;
} cases[] = {
  {"decode + passthrough", "xn--bcher-kva.example"},
  {"non-ASCII label", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.jp"},
  {"already ASCII", "www.example.com"},
  {"needs normalizing", "cafe\xcc\x81.example"},
  {"many labels", "a.b.c.d.e.f.g.h.i.j.k.l.example.com"},
  {"long ASCII label", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.jp"},
};

#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))

// The number of conversions timed per case, and the number of runs to take the
// best of. Timing the best of several runs rejects the noise of a machine that
// is not perfectly idle.
#define ITERATIONS 1000000

#define RUNS 5

static double
now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec * 1e9 + (double) ts.tv_nsec;
}

int
main() {
  for (size_t c = 0; c < CASE_COUNT; c++) {
    const char *domain = cases[c].domain;

    size_t len = 0;
    while (domain[len] != '\0') len++;

    utf8_string_view_t view = utf8_string_view_init((const utf8_t *) domain, len);

    double best = 0;

    for (size_t run = 0; run < RUNS; run++) {
      double start = now_ns();

      for (size_t i = 0; i < ITERATIONS; i++) {
        utf8_string_t result;
        utf8_string_init(&result);

        idna_url_to_ascii(view, &result);

        utf8_string_destroy(&result);
      }

      double elapsed = now_ns() - start;

      double per_op = elapsed / (double) ITERATIONS;

      if (run == 0 || per_op < best) best = per_op;
    }

    printf("%-24s %7.2f ns/op\n", cases[c].name, best);
  }

  return 0;
}
