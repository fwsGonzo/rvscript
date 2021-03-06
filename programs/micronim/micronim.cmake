cmake_minimum_required(VERSION 3.11.0)
project(builder C)
#set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LTO         "Link-time optimizations" ON)
option(GCSECTIONS  "Garbage collect empty sections" OFF)
option(DEBUGGING   "Add debugging information" OFF)
set(VERSION_FILE   "symbols.map" CACHE STRING "Retained symbols file")
option(STRIP_SYMBOLS "Remove all symbols except the public API" ON)

#
# Build configuration
#
set (BINPATH "${CMAKE_SOURCE_DIR}")
set (APIPATH "${CMAKE_CURRENT_LIST_DIR}/../micro/api")

if (GCC_TRIPLE STREQUAL "riscv32-unknown-elf")
	set(RISCV_ABI "-march=rv32g -mabi=ilp32d")
else()
	set(RISCV_ABI "-march=rv64g -mabi=lp64d")
endif()
set(WARNINGS  "-Wall -Wextra -Werror=return-type -Wno-unused")
set(COMMON    "-fno-math-errno -fno-stack-protector")
if (DEBUGGING)
	set (COMMON "${COMMON} -ggdb3 -O0")
endif()
set(FLAGS "${WARNINGS} ${RISCV_ABI} ${COMMON}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Ttext 0x120000")

if (LTO AND NOT DEBUGGING)
	set(FLAGS "${FLAGS} -flto -ffat-lto-objects")
endif()

if (GCSECTIONS AND NOT DEBUGGING)
	set(FLAGS "${FLAGS} -ffunction-sections -fdata-sections")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-gc-sections")
endif()

set(CMAKE_C_FLAGS   "-std=c11 ${FLAGS}")
set(BUILD_SHARED_LIBS OFF)

function (add_verfile NAME VERFILE)
	set_target_properties(${NAME} PROPERTIES LINK_DEPENDS ${VERFILE})
	target_link_libraries(${NAME} "-Wl,--retain-symbols-file=${VERFILE}")
	if (TRUE)
		file(STRINGS "${VERFILE}" SYMBOLS)
		foreach(SYMBOL ${SYMBOLS})
			if (NOT ${SYMBOL} STREQUAL "")
				#message(STATUS "Symbol retained: ${SYMBOL}")
				target_link_libraries(${NAME} "-Wl,--undefined=${SYMBOL}")
			endif()
		endforeach()
	endif()
endfunction()

set(CHPERM  ${CMAKE_CURRENT_LIST_DIR}/chperm)

function (add_micronim_binary NAME VERFILE)
	add_executable(${NAME} ${ARGN}
		env/ffi.c
		env/heap.c
		env/libc.c
		env/nim.c
		env/write.c
	)
	set_source_files_properties(env/libc.c env/heap.c
		PROPERTIES COMPILE_FLAGS -fno-builtin)
	target_link_libraries(${NAME} -static -static-libgcc)
	target_include_directories(${NAME} PUBLIC "${APIPATH}")
	target_include_directories(${NAME} PRIVATE "env")
	# place ELF into the sub-projects source folder
	set_target_properties(${NAME}
		PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
	# strip symbols but keep public API
	if (TRUE)
		if (NOT DEBUGGING)
		set(VERFILE "${CMAKE_CURRENT_SOURCE_DIR}/${VERFILE}")
		if (EXISTS "${VERFILE}")
			add_verfile(${NAME} ${VERFILE})
		else()
			add_verfile(${NAME} ${CMAKE_CURRENT_LIST_DIR}/symbols.map)
		endif()
		add_custom_command(TARGET ${NAME} POST_BUILD
		COMMAND ${CMAKE_STRIP} --strip-debug -R .note -R .comment -- ${CMAKE_BINARY_DIR}/${NAME})
		endif()
	endif()
endfunction()
