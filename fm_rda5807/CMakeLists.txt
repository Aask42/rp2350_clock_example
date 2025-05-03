# Add the source files for the fm_rda5807 library
aux_source_directory(. DIR_fm_rda5807_SRCS)

# Create the fm_rda5807 library
add_library(fm_rda5807 ${DIR_fm_rda5807_SRCS})

# Add the include directory for this library
target_include_directories(fm_rda5807 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link any required libraries (if needed)
target_link_libraries(fm_rda5807 PUBLIC pico_stdlib hardware_i2c)