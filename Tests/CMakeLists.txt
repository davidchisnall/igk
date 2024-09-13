message(STATUS "Adding tests")
file(GLOB TESTS "*.in")

foreach(TEST ${TESTS})
	cmake_path(GET TEST STEM PASS_NAME)
	cmake_path(GET TEST STEM LAST_ONLY TEST_NAME)
	add_test(${TEST_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/testexpected.sh" "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}" ${PASS_NAME})
	message(STATUS "Adding test ${TEST_NAME}")
endforeach(TEST)

