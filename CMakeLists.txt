cmake_minimum_required(VERSION 3.10)
project(ThingsIDoWhenIAmBored)

add_executable(tuicraft 
  tuicraft/tuicraft.cpp
)
add_custom_target(tuicraft_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/tuicraft/resources ${CMAKE_BINARY_DIR}/resources
    COMMENT "Copying resources directory to build directory"
)
add_dependencies(tuicraft tuicraft_resources)

add_executable(tuidash
  tuidash/tuidash.cpp
)
add_custom_target(tuidash_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/tuidash/resources ${CMAKE_BINARY_DIR}/resources
    COMMENT "Copying resources directory to build directory"
)
add_dependencies(tuidash tuidash_resources)


add_executable(snake
  snake/snake.cpp
)
