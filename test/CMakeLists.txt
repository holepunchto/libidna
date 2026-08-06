list(APPEND tests
  url-to-ascii
)

foreach(test IN LISTS tests)
  add_executable(${test} ${test}.c)

  target_link_libraries(
    ${test}
    PRIVATE
      idna_static
  )

  add_test(
    NAME ${test}
    COMMAND ${test}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )

  set_tests_properties(
    ${test}
    PROPERTIES
    TIMEOUT 30
  )
endforeach()

add_executable(conformance conformance.c)

target_link_libraries(
  conformance
  PRIVATE
    idna_static
)

add_test(
  NAME conformance
  COMMAND conformance "${ucd_data}/IdnaTestV2.txt"
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

set_tests_properties(
  conformance
  PROPERTIES
  TIMEOUT 60
)

add_subdirectory(fuzz)
