# CMake script to embed a binary file as a C++ array
# Usage: cmake -DINPUT_FILE=... -DOUTPUT_FILE=... -DVARIABLE_NAME=... -P embed_binary.cmake

file(READ ${INPUT_FILE} binary_data HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_values ${binary_data})
string(REGEX REPLACE ",$" "" hex_values ${hex_values})

file(WRITE ${OUTPUT_FILE}
	"// Auto-generated file - do not edit\n"
	"// This file contains the embedded ${INPUT_FILE} binary data\n"
	"#include <cstddef>\n"
	"#include <cstdint>\n"
	"\n"
	"extern const uint8_t ${VARIABLE_NAME}[] = {\n"
	"${hex_values}\n"
	"};\n"
	"\n"
	"extern const size_t ${VARIABLE_NAME}_size = sizeof(${VARIABLE_NAME});\n"
)

