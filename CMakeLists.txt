cmake_minimum_required(VERSION 3.31)

project(imgPrcsng VERSION 1.0 LANGUAGES CXX)


find_package(OpenCV REQUIRED)

find_library(FFTW_LIB NAMES fftw3 REQUIRED)

add_library(AR_bmp src/bmp_handling.cpp)

# Specify another folder with a CMakeLists.txt file
#add_subdirectory(Course/HW)

# 2. Include Directories for header files 
include_directories(${CMAKE_SOURCE_DIR}/include)


# Specify Source files
set(SOURCES 
	main.cpp
	src/bmp_handling.cpp
	src/cv_bridge.cpp
	src/filters.cpp
	src/histogram_utils.cpp
	src/hsi_handling.cpp
	src/img_manip.cpp
	src/spatial_kernels.cpp


)


# Create executable program 
add_executable(imgPrcsng ${SOURCES})


# Link libraries
target_link_libraries(imgPrcsng PRIVATE AR_bmp ${OpenCV_LIBS} ${FFTW_LIB})



# Set Compiler Properties

target_include_directories(imgPrcsng PUBLIC include src)
set_target_properties(imgPrcsng PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
