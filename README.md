# libidna

Unicode IDNA compatibility processing in C, as specified by [UTS #46](https://www.unicode.org/reports/tr46). Converts a domain name to the ASCII form that DNS carries, mapping and normalizing it and encoding each label with [Punycode](https://github.com/holepunchto/libpunycode) as needed.

## Usage

```c
#include <idna.h>
#include <utf.h>
#include <utf/string.h>

utf8_string_t result;
utf8_string_init(&result);

idna_url_to_ascii(utf8_string_view_init((utf8_t *) "bücher.example", 15), &result);

utf8_string_destroy(&result);
```

## API

See [`include/idna.h`](include/idna.h) for the public API.

Only nontransitional ToASCII is provided, with the flags that a URL parser needs: CheckBidi and CheckJoiners set to true and UseSTD3ASCIIRules, CheckHyphens, VerifyDnsLength, Transitional_Processing, and IgnoreInvalidPunycode all set to false.

A domain for which UTS #46 records an error is rejected rather than converted as far as is possible, there being nothing a URL parser can do with a domain it may not use.

## License

Apache-2.0
