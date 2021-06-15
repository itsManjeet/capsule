add_executable(lexer_test lexer_test.cc)
add_test("LexerTest" lexer_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})