cmake_minimum_required(VERSION 3.31)

project(imgPrcsng VERSION 1.0 LANGUAGES CXX)


find_package(OpenCV REQUIRED)

add_library(AR_bmp src/bmp_handling.cpp)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES 

main.cpp
src/bmp_handling.cpp
src/cv_bridge.cpp
)

add_executable(imgPrcsng ${SOURCES})

target_include_directories(imgPrcsng PUBLIC include src)
target_link_libraries(imgPrcsng PRIVATE AR_bmp ${OpenCV_LIBS})
#set_target_properties(imgPrcsng PROPERTIES LINKER_LANGUAGE CXX)
