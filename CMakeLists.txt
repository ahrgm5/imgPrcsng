cmake_minimum_required(VERSION 3.31)
project(imgPrcsng VERSION 1.0 LANGUAGES C)

# --- Dependencies ---
find_library(FFTW_LIB NAMES fftw3 REQUIRED)

# --- Include Directories ---
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- Source Files ---
set(SOURCES 
    main.c
    src/bmp.c
    src/transforms.c
    src/imgproc.c
)

# --- Executable & Linking ---
add_executable(imgPrcsng ${SOURCES})
target_link_libraries(imgPrcsng PRIVATE ${FFTW_LIB} m)

# --- Compiler Standards ---
target_include_directories(imgPrcsng PUBLIC include src)
set_target_properties(imgPrcsng PROPERTIES 
    C_STANDARD 11 
    C_STANDARD_REQUIRED ON 
    C_EXTENSIONS OFF
)