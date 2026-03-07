cmake_minimum_required(VERSION 3.31)

macro(add_latex_hw TARGET_NAME HW_DIR TEX_FOLDER TEX_FILE)
    # Define paths
    set(TEX_WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${HW_DIR}/${TEX_FOLDER})
    # The expected output PDF will be inside the tex folder
    set(PDF_OUTPUT_FILE ${TEX_WORK_DIR}/${TEX_FILE})
    string(REPLACE ".tex" ".pdf" PDF_OUTPUT_FILE ${PDF_OUTPUT_FILE})

    add_custom_command(
        OUTPUT ${PDF_OUTPUT_FILE}
        # Removed -output-directory=.. so everything stays in the current directory
        COMMAND pdflatex -shell-escape -interaction=nonstopmode ${TEX_FILE}
        COMMAND pdflatex -shell-escape -interaction=nonstopmode ${TEX_FILE}
        DEPENDS ${TEX_WORK_DIR}/${TEX_FILE}
        WORKING_DIRECTORY ${TEX_WORK_DIR}
        COMMENT "Generating PDF inside ${TEX_FOLDER}"
        VERBATIM
    )

    add_custom_target(${TARGET_NAME} DEPENDS ${PDF_OUTPUT_FILE})
endmacro()

# Register assignment
add_latex_hw(hw1_report "HW1" "tex" "imgPrcsngHW1.tex")

# Clean target (updated to look inside the tex folder specifically)
add_custom_target(clean_latex
    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning LaTeX files in tex/ folders..."
    COMMAND find . -name "*.aux" -delete
    COMMAND find . -name "*.log" -delete
    COMMAND find . -name "*.out" -delete
    COMMAND find . -name "*.toc" -delete
    COMMAND find . -name "*.pdf" -delete
    COMMAND find . -type d -name "_minted*" -exec rm -rf {} +
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)