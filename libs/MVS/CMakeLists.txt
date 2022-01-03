# Find required packages

if (WIN32)
    include_directories("C:/3d/auxiliary/gmp/include")
endif()

FIND_PACKAGE(CGAL ${SYSTEM_PACKAGE_REQUIRED} PATHS /usr/local/Cellar/cgal/5.3.1)

if(CGAL_FOUND)
	include_directories(${CGAL_INCLUDE_DIRS})
	add_definitions(${CGAL_DEFINITIONS})
	link_directories(${CGAL_LIBRARY_DIRS})
	message (" Cgal lib dir : ${CGAL_LIBRARY_DIRS} ")
	message (" Cgal lib : ${CGAL_LIBS} ")
endif()

if (APPLE)
    include_directories("/3d/vcglib")
else (APPLE)
    FIND_PACKAGE(VCG REQUIRED)
    if(VCG_FOUND)
        include_directories(${VCG_INCLUDE_DIRS})
        add_definitions(${VCG_DEFINITIONS})
    endif()
endif(APPLE)

set(CERES_LIBS "")
if(OpenMVS_USE_CERES)
	FIND_PACKAGE(Ceres)
	if(CERES_FOUND)
		include_directories(${CERES_INCLUDE_DIRS})
		add_definitions(${CERES_DEFINITIONS})
	else()
		set(OpenMVS_USE_CERES OFF)
		message("-- Can't find CERES. Continuing without it.")
	endif()
endif()

# List sources files
FILE(GLOB LIBRARY_FILES_C "*.cpp")
FILE(GLOB LIBRARY_FILES_H "*.h" "*.inl")
if(_USE_CUDA)
	FILE(GLOB LIBRARY_FILES_CUDA "*.cu")
	LIST(APPEND LIBRARY_FILES_C ${LIBRARY_FILES_CUDA})
endif()

cxx_library_with_type(MVS "Libs" "" "${cxx_default}"
	${LIBRARY_FILES_C} ${LIBRARY_FILES_H}
)

# Manually set Common.h as the precompiled header
IF(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16.0)
	TARGET_PRECOMPILE_HEADERS(MVS PRIVATE "Common.h")
endif()

# Link its dependencies
if(_USE_CUDA)
	set_target_properties(MVS PROPERTIES CUDA_ARCHITECTURES "50;72;75")
endif()
IF (WIN32)
    #TARGET_LINK_LIBRARIES(MVS PRIVATE Common Math IO CGAL::CGAL ${CERES_LIBRARIES} ${CUDA_CUDA_LIBRARY})
    TARGET_LINK_LIBRARIES(MVS PRIVATE Common Math IO ${CERES_LIBS} ${CGAL_LIBS} ${CUDA_CUDA_LIBRARY} C:/3d/auxiliary/gmp/lib/libgmp-10.lib)
else (WIN32)
    #TARGET_LINK_LIBRARIES(MVS PRIVATE Common Math IO ${CERES_LIBS} ${CGAL_LIBS} ${CUDA_CUDA_LIBRARY} gmp z)
    TARGET_LINK_LIBRARIES(MVS PRIVATE Common Math IO CGAL::CGAL ${CERES_LIBRARIES} ${CUDA_CUDA_LIBRARY})
endif (WIN32)

# Install
SET_TARGET_PROPERTIES(MVS PROPERTIES
	PUBLIC_HEADER "${LIBRARY_FILES_H}")
INSTALL(TARGETS MVS
	EXPORT OpenMVSTargets
	LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
	ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
	RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
	PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/MVS")