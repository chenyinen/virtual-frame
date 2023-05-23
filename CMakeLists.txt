cmake_minimum_required (VERSION 2.8)

project (queue)

set(QUEUE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/queue/")

include_directories("${QUEUE_DIR}")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")


add_library(emu_coder SHARED  ${QUEUE_DIR}/queue.c ${QUEUE_DIR}/queue_internal.c emu_coder.c)
add_library(emu_coder_static STATIC  ${QUEUE_DIR}/queue.c ${QUEUE_DIR}/queue_internal.c emu_coder.c)
add_executable (virtual_stream example.c)

target_link_libraries (virtual_stream emu_coder_static avcodec avformat avutil swscale pthread)


