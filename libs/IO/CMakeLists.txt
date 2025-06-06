# Find required packages
FIND_PACKAGE(PNG)
if(PNG_FOUND)
	INCLUDE_DIRECTORIES(${PNG_INCLUDE_DIRS})
	ADD_DEFINITIONS(${PNG_DEFINITIONS})
	SET(_USE_PNG TRUE CACHE INTERNAL "")
	message (STATUS "PNG Ok")
else()
	if (WIN32)
		INCLUDE_DIRECTORIES("C:/3d/opencv/3rdparty/libpng;C:/3d/opencv/3rdparty/zlib")
		SET(_USE_PNG TRUE CACHE INTERNAL "")
		ADD_DEFINITIONS(${PNG_DEFINITIONS} -D_USE_PNG)
		SET(PNG_LIBRARIES "C:/3d/builds/opencv/3rdparty/lib/Release/libpng.lib;C:/3d/builds/opencv/3rdparty/lib/Release/zlib.lib")
		message ("PNG Ok (écrit en dur dans le libs/io/CMakelist.txt")
	else(WIN32)
		message(STATUS "PNG Lib not found")
		SET(PNG_LIBRARIES "")
	endif(WIN32)
endif()

FIND_PACKAGE(JPEG QUIET)
if(JPEG_FOUND)
	INCLUDE_DIRECTORIES(${JPEG_INCLUDE_DIR})
	ADD_DEFINITIONS(${JPEG_DEFINITIONS})
	SET(_USE_JPG TRUE CACHE INTERNAL "")
else()
	INCLUDE_DIRECTORIES("C:/3d/opencv/3rdparty/libjpeg-turbo/src")
	INCLUDE_DIRECTORIES("C:/3d/builds/opencv/3rdparty/libjpeg-turbo")
	ADD_DEFINITIONS(-D_USE_JPG)
	message ("impossible de trouver JPG => remplacé par celui de OpenCV")
	SET(JPEG_LIBRARIES "C:/3d/builds/opencv/3rdparty/lib/Release/libjpeg-turbo.lib")
endif()
FIND_PACKAGE(TIFF QUIET)
if(TIFF_FOUND)
	INCLUDE_DIRECTORIES(${TIFF_INCLUDE_DIR})
	ADD_DEFINITIONS(${TIFF_DEFINITIONS})
	SET(_USE_TIFF TRUE CACHE INTERNAL "")
else()
	SET(TIFF_LIBRARIES "")
endif()

# List sources files
FILE(GLOB LIBRARY_FILES_C "*.cpp")
FILE(GLOB LIBRARY_FILES_H "*.h" "*.inl")

cxx_library_with_type(IO "Libs" "" "${cxx_default}"
	${LIBRARY_FILES_C} ${LIBRARY_FILES_H}
)

# Manually set Common.h as the precompiled header
IF(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16.0)
	TARGET_PRECOMPILE_HEADERS(IO PRIVATE "Common.h")
endif()

# Link its dependencies
TARGET_LINK_LIBRARIES(IO Common ${PNG_LIBRARIES} ${JPEG_LIBRARIES} ${TIFF_LIBRARIES} ${EXIV2_LIBS})

# Install
SET_TARGET_PROPERTIES(IO PROPERTIES
	PUBLIC_HEADER "${LIBRARY_FILES_H}")
INSTALL(TARGETS IO
	EXPORT OpenMVSTargets
	LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
	ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
	RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
	PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/IO")
