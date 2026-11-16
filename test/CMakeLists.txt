if(TARGET idna_shared)
  set(library idna::shared)
else()
  set(library idna::static)
endif()

add_test_executables(
  url-to-ascii.c
  LINK
    ${library}
)

add_test_executable(
  conformance
  conformance.c
  LINK
    ${library}
  ARGS
    "${ucd_data}/IdnaTestV2.txt"
  TIMEOUT 60
)

add_subdirectory(fuzz)
