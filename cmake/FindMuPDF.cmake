SET(MuPDF_INCLUDE_DIR MuPDF_LIBRARY)
#
### Look for the include files.
#
find_path(
    MuPDF_INCLUDE_DIR
    NAMES mupdf/fitz.h
    HINTS ${CMAKE_PREFIX_PATH}/include
    DOC "MuPDF include directory"
)
#
### Look for the libraries
#
set(_MuPDF_LIBRARY_NAMES mupdf)
if (PLATFORM_SWITCH OR PLATFORM_PSV)
    list(APPEND _MuPDF_LIBRARY_NAMES mupdf-third jpeg)
endif ()
foreach(l ${_MuPDF_LIBRARY_NAMES})
    find_library(
        MuPDF_LIBRARY_${l}
        NAMES ${l}
        HINTS ${CMAKE_PREFIX_PATH}/lib
        PATH_SUFFIXES lib${LIB_SUFFIX}
    )
    list(APPEND MuPDF_LIBRARY ${MuPDF_LIBRARY_${l}})
endforeach ()

get_filename_component(_MuPDF_LIBRARY_DIR ${MuPDF_LIBRARY_mupdf} PATH)

set(MuPDF_LIBRARY_DIRS _MuPDF_LIBRARY_DIR)
list(REMOVE_DUPLICATES MuPDF_LIBRARY_DIRS)
mark_as_advanced(
    MuPDF_LIBRARY
    MuPDF_INCLUDE_DIR
    MuPDF_LIBRARY_DIRS
)
set(MuPDF_INCLUDE_DIRS ${MuPDF_INCLUDE_DIR})

#
### Check if everything was found and if the version is sufficient.
#
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MuPDF
    FOUND_VAR MuPDF_FOUND
    REQUIRED_VARS MuPDF_LIBRARY MuPDF_INCLUDE_DIR
)