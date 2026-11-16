add_fuzz_executables(
  url-to-ascii.c
  PREFIX fuzz_
  DURATION 600
  LINK
    idna::static
)
