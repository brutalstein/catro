add_library(catro_video STATIC
    src/rtp_h264.cpp
)

target_include_directories(catro_video
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

catro_enable_warnings(catro_video)

add_library(Catro::Video ALIAS catro_video)
