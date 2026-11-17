add_test_executables(
  url-to-ascii.c
  LINK
    idna::<TYPE>
)

add_test_executable(
  conformance
  conformance.c
  LINK
    idna::<TYPE>
  ARGS
    "${ucd_data}/IdnaTestV2.txt"
  TIMEOUT 60
)

add_subdirectory(fuzz)
