aux_source_directory(. DIR_radio_ctrl_SRCS)

add_library(radio_ctrl ${DIR_radio_ctrl_SRCS})

# Add the include directory for fm_rda5807
target_include_directories(radio_ctrl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../fm_rda5807/include ../rds_parser/include)

# Link the required libraries
target_link_libraries(radio_ctrl PUBLIC pico_stdlib hardware_i2c fm_rda5807 rds_parser)