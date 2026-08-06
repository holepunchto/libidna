list(APPEND benches
  url-to-ascii
)

foreach(bench IN LISTS benches)
  add_executable(bench_${bench} ${bench}.c)

  target_link_libraries(
    bench_${bench}
    PRIVATE
      idna_static
  )
endforeach()
