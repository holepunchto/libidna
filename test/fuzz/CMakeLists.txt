list(APPEND fuzzers
  url-to-ascii
)

foreach(fuzzer IN LISTS fuzzers)
  string(REPLACE "/" "_" name ${fuzzer})

  add_fuzzer(
    fuzz_${name}
    ${fuzzer}.c
    DURATION 600
  )

  target_link_libraries(
    fuzz_${name}
    PRIVATE
      idna_static
  )
endforeach()
