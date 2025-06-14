# Defines functions and macros useful for building the application.
#
# Note:
#
# - This file can be run multiple times, therefore it shouldn't
#   have any side effects other than defining the functions and macros.

INCLUDE(CheckCXXCompilerFlag)
INCLUDE(CheckIncludeFile)

# BUILD_SHARED_LIBS is a standard CMake variable, but we declare it here to
# make it prominent in the GUI.
OPTION(BUILD_SHARED_LIBS "Build shared libraries (DLLs)" OFF)
OPTION(BUILD_SHARED_LIBS_FULL "Expose all functionality when built as shared libraries (DLLs)" OFF)
OPTION(BUILD_EXCEPTIONS_ENABLED "Enable support for exceptions" ON)
OPTION(BUILD_RTTI_ENABLED "Enable support run-time type information" ON)
OPTION(BUILD_STATIC_RUNTIME "Link staticaly the run-time library" OFF)
OPTION(CMAKE_SUPPRESS_REGENERATION "This will cause CMake to not put in the rules that re-run CMake. This might be useful if you want to use the generated build files on another machine" OFF)
OPTION(CMAKE_USE_RELATIVE_PATHS "Try to use relative paths in generated projects" OFF)

# Organize projects into folders
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)
SET_PROPERTY(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
SET(COTIRE_TARGETS_FOLDER "CMakeTargets")


# GetOperatingSystemArchitectureBitness(<var-prefix>)
# is used to extract information associated with the current platform.
#
# The macro defines the following variables:
# <var-prefix>_BITNESS - bitness of the platform: 32 or 64
# <var-prefix>_OS - which is on the this value: linux, macosx, win
# <var-prefix>_ARCHITECTURE - which is on the this value: i386, amd64, ppc

set(OS_NAME_LINUX "linux")
set(OS_NAME_MAC "macosx")
set(OS_NAME_WIN "win")

macro(GetOperatingSystemArchitectureBitness)
	set(MY_VAR_PREFIX "${ARGN}")

	# Sanity checks
	if("${MY_VAR_PREFIX}" STREQUAL "")
		message(FATAL_ERROR "error: VAR_PREFIX should be specified !")
	endif()

	set(${MY_VAR_PREFIX}_ARCHITECTURE "")

	set(${MY_VAR_PREFIX}_ARCHITECTURE i386)
	set(${MY_VAR_PREFIX}_BITNESS 32)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(${MY_VAR_PREFIX}_BITNESS 64)
		set(${MY_VAR_PREFIX}_ARCHITECTURE amd64)
	endif()

	if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set(${MY_VAR_PREFIX}_OS "${OS_NAME_WIN}")
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(${MY_VAR_PREFIX}_OS "${OS_NAME_LINUX}")
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		set(${MY_VAR_PREFIX}_OS "${OS_NAME_MAC}")
		if(CMAKE_SYSTEM_PROCESSOR MATCHES "powerpc")
			set(${MY_VAR_PREFIX}_ARCHITECTURE "ppc")
		endif()
	endif()

	# Detect Microsoft compiler:
	set(MSVC64 0)
	if(CMAKE_CL_64)
		set(MSVC64 1)
	endif()

	set(CLANG 0)
	if(CMAKE_C_COMPILER_ID  MATCHES "^(Apple)?Clang$")
		set(CLANG 1)
		if(NOT APPLE)
			set(CMAKE_COMPILER_IS_GNUCC 1)
		endif()
	endif()
	if(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
	math(EXPR CLANG "${CLANG}+2")
		if(NOT APPLE)
			set(CMAKE_COMPILER_IS_GNUCXX 1)
		endif()
	endif()

	# Detect Intel ICC compiler -- for -fPIC in 3rdparty ( UNIX ONLY ):
	#  the system needs to determine if the '-fPIC' option needs to be added
	#  for the 3rdparty static libs being compiled; use the FLG_ICC definition
	#  being set here to determine if the -fPIC flag should be used
	if(UNIX)
		if  (__ICL)
			set(FLG_ICC   __ICL)
		elseif(__ICC)
			set(FLG_ICC   __ICC)
		elseif(__ECL)
			set(FLG_ICC   __ECL)
		elseif(__ECC)
			set(FLG_ICC   __ECC)
		elseif(__INTEL_COMPILER)
			set(FLG_ICC   __INTEL_COMPILER)
		elseif(CMAKE_C_COMPILER MATCHES "icc")
			set(FLG_ICC   __ICC_C_COMPILER)
		endif()
	endif()

	if(MSVC AND CMAKE_C_COMPILER MATCHES "icc")
		set(FLG_ICC __INTEL_COMPILER_FOR_WINDOWS)
	endif()

	if(CMAKE_COMPILER_IS_GNUCXX OR ${CLANG} GREATER 1 OR (UNIX AND FLG_ICC))
	  set(FLG_COMPILER_IS_GNU TRUE)
	else()
	  set(FLG_COMPILER_IS_GNU FALSE)
	endif()

	# Detect GNU version:
	set(CMAKE_FLG_GCC_VERSION_NUM 0)
	if(CMAKE_COMPILER_IS_GNUCXX)
		execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version
					  OUTPUT_VARIABLE CMAKE_FLG_GCC_VERSION_FULL
					  OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(CMAKE_FLG_GCC_VERSION_FULL STREQUAL "")
			execute_process(COMMAND ${CMAKE_CXX_COMPILER} -v
						  ERROR_VARIABLE CMAKE_FLG_GCC_VERSION_FULL
						  OUTPUT_STRIP_TRAILING_WHITESPACE)
		endif()

		# Typical output in CMAKE_FLG_GCC_VERSION_FULL: "c+//0 (whatever) 4.2.3 (...)"
		# Look for the version number
		string(REGEX MATCH "[0-9]+.[0-9]+.[0-9]+" CMAKE_GCC_REGEX_VERSION "${CMAKE_FLG_GCC_VERSION_FULL}")
		if(NOT CMAKE_GCC_REGEX_VERSION)
		  string(REGEX MATCH "[0-9]+.[0-9]+" CMAKE_GCC_REGEX_VERSION "${CMAKE_FLG_GCC_VERSION_FULL}")
		endif()

		# Split the three parts:
		string(REGEX MATCHALL "[0-9]+" CMAKE_FLG_GCC_VERSIONS "${CMAKE_GCC_REGEX_VERSION}")

		list(GET CMAKE_FLG_GCC_VERSIONS 0 CMAKE_FLG_GCC_VERSION_MAJOR)
		list(GET CMAKE_FLG_GCC_VERSIONS 1 CMAKE_FLG_GCC_VERSION_MINOR)

		set(CMAKE_FLG_GCC_VERSION ${CMAKE_FLG_GCC_VERSION_MAJOR}${CMAKE_FLG_GCC_VERSION_MINOR})
		math(EXPR CMAKE_FLG_GCC_VERSION_NUM "${CMAKE_FLG_GCC_VERSION_MAJOR}*100 + ${CMAKE_FLG_GCC_VERSION_MINOR}")
		message(STATUS "Detected version of GNU GCC: ${CMAKE_FLG_GCC_VERSION} (${CMAKE_FLG_GCC_VERSION_NUM})")

		if(WIN32)
			execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
					  OUTPUT_VARIABLE CMAKE_FLG_GCC_TARGET_MACHINE
					  OUTPUT_STRIP_TRAILING_WHITESPACE)
			if(CMAKE_FLG_GCC_TARGET_MACHINE MATCHES "64")
				set(MINGW64 1)
			endif()
		endif()
	endif()

	if(CMAKE_SYSTEM_PROCESSOR MATCHES amd64.*|x86_64.* OR CMAKE_GENERATOR MATCHES "Visual Studio.*Win64")
		set(X86_64 1)
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES i686.*|i386.*|x86.*)
		set(X86 1)
	endif()
	
	if(NOT ${MY_VAR_PREFIX}_PACKAGE_REQUIRED)
		set(${MY_VAR_PREFIX}_PACKAGE_REQUIRED "REQUIRED")
	endif()
endmacro()


# Call this macro in order to create the suffix
# needed when searching the libraries of the linked packages.
macro(ComposePackageLibSuffix)
	set(PACKAGE_LIB_SUFFIX "")
	set(PACKAGE_LIB_SUFFIX_DBG "")
	set(PACKAGE_LIB_SUFFIX_REL "")
	if(MSVC)
		if("${MSVC_VERSION}" STRGREATER "1929")
			set(PACKAGE_LIB_SUFFIX "/vc17")
		elseif("${MSVC_VERSION}" STRGREATER "1916")
			set(PACKAGE_LIB_SUFFIX "/vc16")
		elseif("${MSVC_VERSION}" STRGREATER "1900")
			set(PACKAGE_LIB_SUFFIX "/vc15")
		elseif("${MSVC_VERSION}" STREQUAL "1900")
			set(PACKAGE_LIB_SUFFIX "/vc14")
		elseif("${MSVC_VERSION}" STREQUAL "1800")
			set(PACKAGE_LIB_SUFFIX "/vc12")
		elseif("${MSVC_VERSION}" STREQUAL "1700")
			set(PACKAGE_LIB_SUFFIX "/vc11")
		elseif("${MSVC_VERSION}" STREQUAL "1600")
			set(PACKAGE_LIB_SUFFIX "/vc10")
		elseif("${MSVC_VERSION}" STREQUAL "1500")
			set(PACKAGE_LIB_SUFFIX "/vc9")
		endif()
		if("${SYSTEM_BITNESS}" STREQUAL "64")
			set(PACKAGE_LIB_SUFFIX "${PACKAGE_LIB_SUFFIX}/x64")
		else()
			set(PACKAGE_LIB_SUFFIX "${PACKAGE_LIB_SUFFIX}/x86")
		endif()
		set(PACKAGE_LIB_SUFFIX_DBG "${PACKAGE_LIB_SUFFIX}/Debug")
		set(PACKAGE_LIB_SUFFIX_REL "${PACKAGE_LIB_SUFFIX}/Release")
	endif()
endmacro()


# Call this macro in order to disable SAFESEH in Visual Studio.
macro(DisableSAFESEH)
	if(MSVC)
		if("${MSVC_VERSION}" STRGREATER "1600")
			if(NOT "${SYSTEM_BITNESS}" STREQUAL "64")
				SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
				SET (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
				SET (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SAFESEH:NO")
			endif()
		endif()
	ENDIF()
endmacro()


# Call this whenever there was an error in finding a package
# handle the QUIETLY and REQUIRED arguments and set xxx_FOUND to FALSE
macro(package_report_not_found NAME MSG)
	# make FIND_PACKAGE friendly
	if(NOT ${NAME}_FIND_QUIETLY)
		if(${NAME}_FIND_REQUIRED)
			message(FATAL_ERROR "${NAME} required, but not found: ${MSG}")
		else()
			message(STATUS "WARNING: ${NAME} was not found: ${MSG}")
		endif()
	endif()
	set(${NAME}_FOUND FALSE)
endmacro()


# Provides an option that the user can optionally select.
# Can accept condition to control when option is available for user.
# Usage:
#   option(<option_variable> "help string describing the option" <initial value or boolean expression> [IF <condition>])
macro(add_option variable description value)
  set(__value ${value})
  set(__condition "")
  set(__varname "__value")
  foreach(arg ${ARGN})
    if(arg STREQUAL "IF" OR arg STREQUAL "if")
      set(__varname "__condition")
    else()
      list(APPEND ${__varname} ${arg})
    endif()
  endforeach()
  unset(__varname)
  if(__condition STREQUAL "")
    set(__condition 2 GREATER 1)
  endif()

  if(${__condition})
    if("${__value}" MATCHES ";")
      if(${__value})
        option(${variable} "${description}" ON)
      else()
        option(${variable} "${description}" OFF)
      endif()
    elseif(DEFINED ${__value})
      if(${__value})
        option(${variable} "${description}" ON)
      else()
        option(${variable} "${description}" OFF)
      endif()
    else()
      option(${variable} "${description}" ${__value})
    endif()
  else()
    unset(${variable} CACHE)
  endif()
  unset(__condition)
  unset(__value)
endmacro()


# Optimize compiler settings

set(STATIC_COMPILER_FAIL_REGEX
    "command line option .* is valid for .* but not for C\\+\\+" # GNU
    "command line option .* is valid for .* but not for C" # GNU
    "unrecognized .*option"                     # GNU
    "unknown .*option"                          # Clang
    "ignoring unknown option"                   # MSVC
    "warning D9002"                             # MSVC, any lang
    "option .*not supported"                    # Intel
    "[Uu]nknown option"                         # HP
    "[Ww]arning: [Oo]ption"                     # SunPro
    "command option .* is not recognized"       # XL
    "not supported in this configuration; ignored"       # AIX
    "File with unknown suffix passed to linker" # PGI
    "WARNING: unknown flag:"                    # Open64
  )

MACRO(_check_compiler_flag LANG FLAG RESULT)
  if(NOT DEFINED ${RESULT})
    if("_${LANG}_" MATCHES "_CXX_")
      set(_fname "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.cxx")
      if("${CMAKE_CXX_FLAGS} ${FLAG} " MATCHES "-Werror " OR "${CMAKE_CXX_FLAGS} ${FLAG} " MATCHES "-Werror=unknown-pragmas ")
        FILE(WRITE "${_fname}" "int main() { return 0; }\n")
      else()
        FILE(WRITE "${_fname}" "#pragma\nint main() { return 0; }\n")
      endif()
    elseif("_${LANG}_" MATCHES "_C_")
      set(_fname "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.c")
      if("${CMAKE_C_FLAGS} ${FLAG} " MATCHES "-Werror " OR "${CMAKE_C_FLAGS} ${FLAG} " MATCHES "-Werror=unknown-pragmas ")
        FILE(WRITE "${_fname}" "int main(void) { return 0; }\n")
      else()
        FILE(WRITE "${_fname}" "#pragma\nint main(void) { return 0; }\n")
      endif()
    else()
      unset(_fname)
    endif()
    if(_fname)
      MESSAGE(STATUS "Performing Test ${RESULT}")
      TRY_COMPILE(${RESULT}
        ${CMAKE_BINARY_DIR}
        "${_fname}"
        COMPILE_DEFINITIONS "${FLAG}"
        OUTPUT_VARIABLE OUTPUT)

      FOREACH(_regex ${STATIC_COMPILER_FAIL_REGEX})
        IF("${OUTPUT}" MATCHES "${_regex}")
          SET(${RESULT} 0)
          break()
        ENDIF()
      ENDFOREACH()

      IF(${RESULT})
        SET(${RESULT} 1 CACHE INTERNAL "Test ${RESULT}")
        MESSAGE(STATUS "Performing Test ${RESULT} - Success")
      ELSE(${RESULT})
        MESSAGE(STATUS "Performing Test ${RESULT} - Failed")
        SET(${RESULT} "" CACHE INTERNAL "Test ${RESULT}")
      ENDIF(${RESULT})
    else()
      SET(${RESULT} 0)
    endif()
  endif()
ENDMACRO()

macro(_check_flag_support lang flag varname)
  if("_${lang}_" MATCHES "_CXX_")
    set(_lang CXX)
  elseif("_${lang}_" MATCHES "_C_")
    set(_lang C)
  else()
    set(_lang ${lang})
  endif()

  string(TOUPPER "${flag}" ${varname})
  string(REGEX REPLACE "^(/|-)" "HAVE_${_lang}_" ${varname} "${${varname}}")
  string(REGEX REPLACE " -|-|=| |\\." "_" ${varname} "${${varname}}")

  _check_compiler_flag("${_lang}" "${ARGN} ${flag}" ${${varname}})
endmacro()

macro(add_extra_compiler_option option)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_BUILD_TYPE})
  endif()
  if(CMAKE_CXX_COMPILER_ID)
    _check_flag_support(CXX "${option}" _varname "${BUILD_EXTRA_CXX_FLAGS} ${ARGN}")
    if(${_varname})
      set(BUILD_EXTRA_CXX_FLAGS "${BUILD_EXTRA_CXX_FLAGS} ${option}")
    endif()
  endif()
  if(CMAKE_C_COMPILER_ID)
    _check_flag_support(C "${option}" _varname "${BUILD_EXTRA_C_FLAGS} ${ARGN}")
    if(${_varname})
      set(BUILD_EXTRA_C_FLAGS "${BUILD_EXTRA_C_FLAGS} ${option}")
    endif()
  endif()
endmacro()

macro(optimize_default_compiler_settings)
	# build options
	# ===================================================
	add_option(ENABLE_PRECOMPILED_HEADERS "Use precompiled headers"                                  ON   IF (NOT IOS) )
	add_option(ENABLE_PROFILING           "Enable profiling in the GCC compiler (Add flags: -g -pg)" OFF  IF CMAKE_COMPILER_IS_GNUCXX )
	add_option(ENABLE_OMIT_FRAME_POINTER  "Enable -fomit-frame-pointer for GCC"                      ON   IF CMAKE_COMPILER_IS_GNUCXX )
	add_option(ENABLE_POWERPC             "Enable PowerPC for GCC"                                   ON   IF (CMAKE_COMPILER_IS_GNUCXX AND CMAKE_SYSTEM_PROCESSOR MATCHES powerpc.*) )
	add_option(ENABLE_FAST_MATH           "Enable -ffast-math (not recommended for GCC 4.6.x)"       OFF  IF (CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSE                 "Enable SSE instructions"                                  ON   IF (MSVC OR CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSE2                "Enable SSE2 instructions"                                 ON   IF (MSVC OR CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSE3                "Enable SSE3 instructions"                                 ON   IF (MSVC OR FLG_ICC OR CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSSE3               "Enable SSSE3 instructions"                                ON   IF (CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSE41               "Enable SSE4.1 instructions"                               ON   IF (FLG_ICC OR CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_SSE42               "Enable SSE4.2 instructions"                               ON   IF (CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_AVX                 "Enable AVX instructions"                                  OFF )
	add_option(ENABLE_AVX2                "Enable AVX2 instructions"                                 OFF  IF (CMAKE_COMPILER_IS_GNUCXX AND (X86 OR X86_64)) )
	add_option(ENABLE_EXTRA_WARNINGS      "Show extra warnings (usually not critical)"               OFF )
	add_option(ENABLE_NOISY_WARNINGS      "Show all warnings even if they are too noisy"             OFF )
	add_option(ENABLE_WARNINGS_AS_ERRORS  "Treat warnings as errors"                                 OFF )

	set(BUILD_EXTRA_FLAGS "")
	set(BUILD_EXTRA_C_FLAGS "")
	set(BUILD_EXTRA_CXX_FLAGS "")
	set(BUILD_EXTRA_FLAGS_RELEASE "")
	set(BUILD_EXTRA_FLAGS_DEBUG "")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS "")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE "")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS_DEBUG "")

	# try to enable C++XX support
	if(CMAKE_VERSION VERSION_LESS "3.8.2")
		if (MSVC)
			set(CXX_CHECK_PREFIX "/std:")
		else()
			set(CXX_CHECK_PREFIX "--std=")
		endif()
		check_cxx_compiler_flag("${CXX_CHECK_PREFIX}c++23" SUPPORTS_STD_CXX23)
		check_cxx_compiler_flag("${CXX_CHECK_PREFIX}c++20" SUPPORTS_STD_CXX20)
		check_cxx_compiler_flag("${CXX_CHECK_PREFIX}c++17" SUPPORTS_STD_CXX17)
		check_cxx_compiler_flag("${CXX_CHECK_PREFIX}c++14" SUPPORTS_STD_CXX14)
		check_cxx_compiler_flag("${CXX_CHECK_PREFIX}c++11" SUPPORTS_STD_CXX11)
		if(SUPPORTS_STD_CXX23)
			set(CMAKE_CXX_STANDARD 23)
		elseif(SUPPORTS_STD_CXX20)
			set(CMAKE_CXX_STANDARD 20)
		elseif(SUPPORTS_STD_CXX17)
			set(CMAKE_CXX_STANDARD 17)
		elseif(SUPPORTS_STD_CXX14)
			set(CMAKE_CXX_STANDARD 14)
		elseif(SUPPORTS_STD_CXX11)
			set(CMAKE_CXX_STANDARD 11)
		endif()
	else()
		list(FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_23" CXX_STD_INDEX)
		if(${CXX_STD_INDEX} GREATER -1)
			set(CMAKE_CXX_STANDARD 23)
		else()
			list(FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_20" CXX_STD_INDEX)
			if(${CXX_STD_INDEX} GREATER -1)
				set(CMAKE_CXX_STANDARD 20)
			else()
				list(FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_17" CXX_STD_INDEX)
				if(${CXX_STD_INDEX} GREATER -1)
					set(CMAKE_CXX_STANDARD 17)
				else()
					list(FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_14" CXX_STD_INDEX)
					if(${CXX_STD_INDEX} GREATER -1)
						set(CMAKE_CXX_STANDARD 14)
					else()
						list(FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_11" CXX_STD_INDEX)
						if(${CXX_STD_INDEX} GREATER -1)
							set(CMAKE_CXX_STANDARD 11)
						endif()
					endif()
				endif()
			endif()
		endif()
	endif()
	if(CLANG AND (CMAKE_CXX_STANDARD EQUAL 11 OR CMAKE_CXX_STANDARD EQUAL 14 OR CMAKE_CXX_STANDARD EQUAL 17 OR CMAKE_CXX_STANDARD EQUAL 20 OR CMAKE_CXX_STANDARD EQUAL 23))
		set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")
		add_extra_compiler_option(-stdlib=libc++)
	endif()
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	message(STATUS "Compiling with C++${CMAKE_CXX_STANDARD}")

	if(FLG_COMPILER_IS_GNU)
	  # High level of warnings.
	  add_extra_compiler_option(-W)
	  add_extra_compiler_option(-Wall)
	  #add_extra_compiler_option(-Werror=return-type)
	  #add_extra_compiler_option(-Werror=non-virtual-dtor)
	  add_extra_compiler_option(-Werror=address)
	  add_extra_compiler_option(-Werror=sequence-point)
	  add_extra_compiler_option(-Wformat)
	  add_extra_compiler_option(-Werror=format-security -Wformat)
	  add_extra_compiler_option(-Winit-self)
	  add_extra_compiler_option(-Wsign-promo)
	  add_extra_compiler_option(-Wreorder)

	  if(ENABLE_NOISY_WARNINGS)
		add_extra_compiler_option(-Wshadow)
		add_extra_compiler_option(-Wextra)
		add_extra_compiler_option(-Wcast-align)
		add_extra_compiler_option(-Wstrict-aliasing=2)
		add_extra_compiler_option(-Wmissing-declarations)
		add_extra_compiler_option(-Wmissing-prototypes)
		add_extra_compiler_option(-Wpointer-arith)
		add_extra_compiler_option(-Wundef)
		add_extra_compiler_option(-Wswitch)
		add_extra_compiler_option(-Wswitch-enum)
		add_extra_compiler_option(-Wswitch-default)
	  else()
		add_extra_compiler_option(-Wno-undef)
		add_extra_compiler_option(-Wno-switch)
		add_extra_compiler_option(-Wno-switch-enum)
		add_extra_compiler_option(-Wno-switch-default)
		add_extra_compiler_option(-Wno-implicit-fallthrough)
		add_extra_compiler_option(-Wno-comment)
		add_extra_compiler_option(-Wno-narrowing)
		add_extra_compiler_option(-Wno-attributes)
		add_extra_compiler_option(-Wno-ignored-attributes)
		add_extra_compiler_option(-Wno-maybe-uninitialized)
		add_extra_compiler_option(-Wno-enum-compare)
		add_extra_compiler_option(-Wno-misleading-indentation)
		add_extra_compiler_option(-Wno-missing-field-initializers)
		add_extra_compiler_option(-Wno-unused-result)
		add_extra_compiler_option(-Wno-unused-function)
		add_extra_compiler_option(-Wno-unused-parameter)
		add_extra_compiler_option(-Wno-delete-incomplete)
		add_extra_compiler_option(-Wno-unnamed-type-template-args)
		add_extra_compiler_option(-Wno-int-in-bool-context)
		add_extra_compiler_option(-Wno-deprecated-declarations)
		add_extra_compiler_option(-Wno-deprecated-anon-enum-enum-conversion)
		add_extra_compiler_option(-Wno-deprecated-enum-compare-conditional)
		add_extra_compiler_option(-Wno-deprecated-enum-enum-conversion)
	  endif()
	  add_extra_compiler_option(-fdiagnostics-show-option)
	  add_extra_compiler_option(-ftemplate-backtrace-limit=0)
	  add_extra_compiler_option(-Wno-missing-template-arg-list-after-template-kw)
	  
	  # The -Wno-long-long is required in 64bit systems when including system headers.
	  if(X86_64)
		add_extra_compiler_option(-Wno-long-long)
	  endif()

	  # We need pthread's
	  if(UNIX AND NOT ANDROID)
		add_extra_compiler_option(-pthread)
	  endif()

	  if(ENABLE_WARNINGS_AS_ERRORS)
		add_extra_compiler_option(-Werror)
	  endif()

	  # Other optimizations
	  if(ENABLE_OMIT_FRAME_POINTER)
		add_extra_compiler_option(-fomit-frame-pointer)
	  else()
		add_extra_compiler_option(-fno-omit-frame-pointer)
	  endif()
	  if(NOT CLANG)
	  if(ENABLE_FAST_MATH)
		add_extra_compiler_option(-ffast-math)
	  else()
		add_extra_compiler_option(-frounding-math)
	  endif()
	  endif()
	  if(ENABLE_POWERPC)
		add_extra_compiler_option("-mcpu=G3 -mtune=G5")
	  endif()
	  if(ENABLE_SSE)
		add_extra_compiler_option(-msse)
	  endif()
	  if(ENABLE_SSE2)
		add_extra_compiler_option(-msse2)
	  endif()

	  # SSE3 and further should be disabled under MingW because it generates compiler errors
	  if(NOT MINGW)
		if(ENABLE_SSE3)
		  add_extra_compiler_option(-msse3)
		endif()

		if(${CMAKE_FLG_GCC_VERSION_NUM} GREATER 402 OR CLANG)
		  set(HAVE_GCC43_OR_NEWER 1)
		endif()
		if(${CMAKE_FLG_GCC_VERSION_NUM} GREATER 401 OR CLANG)
		  set(HAVE_GCC42_OR_NEWER 1)
		endif()

		if(HAVE_GCC42_OR_NEWER OR APPLE)
		  if(ENABLE_SSSE3)
			add_extra_compiler_option(-mssse3)
		  endif()
		  if(HAVE_GCC43_OR_NEWER)
			if(ENABLE_SSE41)
			   add_extra_compiler_option(-msse4.1)
			endif()
			if(ENABLE_SSE42)
			   add_extra_compiler_option(-msse4.2)
			endif()
			if(ENABLE_AVX)
			   add_extra_compiler_option(-mavx)
			endif()
			if(ENABLE_AVX2)
			   add_extra_compiler_option(-mavx2)
			endif()
		  endif()
		endif()
	  endif(NOT MINGW)

	  if(X86 OR X86_64)
		if(NOT APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 4)
		  if(ENABLE_SSE2)
			add_extra_compiler_option(-mfpmath=sse)# !! important - be on the same wave with x64 compilers
		  else()
			add_extra_compiler_option(-mfpmath=387)
		  endif()
		endif()
	  endif()

	  # Profiling?
	  if(ENABLE_PROFILING)
		add_extra_compiler_option("-pg -g")
		# turn off incompatible options
		foreach(flags CMAKE_CXX_FLAGS CMAKE_C_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
					  BUILD_EXTRA_FLAGS_RELEASE BUILD_EXTRA_FLAGS_DEBUG BUILD_EXTRA_C_FLAGS BUILD_EXTRA_CXX_FLAGS)
		  string(REPLACE "-fomit-frame-pointer" "" ${flags} "${${flags}}")
		  string(REPLACE "-ffunction-sections" "" ${flags} "${${flags}}")
		endforeach()
	  elseif(NOT APPLE AND NOT ANDROID)
		# Remove unreferenced functions: function level linking
		add_extra_compiler_option(-ffunction-sections)
	  endif()

	  set(BUILD_EXTRA_FLAGS_RELEASE "${BUILD_EXTRA_FLAGS_RELEASE} -DNDEBUG")
	  set(BUILD_EXTRA_FLAGS_DEBUG "${BUILD_EXTRA_FLAGS_DEBUG} -O0 -D_DEBUG")
	  if(BUILD_WITH_DEBUG_INFO)
		set(BUILD_EXTRA_FLAGS_DEBUG "${BUILD_EXTRA_FLAGS_DEBUG} -ggdb3")
	  endif()
	endif()

	if(MSVC)
	  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /D _CRT_SECURE_NO_DEPRECATE /D _CRT_NONSTDC_NO_DEPRECATE /D _SCL_SECURE_NO_WARNINGS")
	  # 64-bit portability warnings, in MSVC80
	  if(MSVC80)
		set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /Wp64")
	  endif()

	  if(BUILD_WITH_DEBUG_INFO)
		set(BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE "${BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE} /debug")
	  endif()

	  # Remove unreferenced functions: function level linking
	  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /Gy")
	  if(NOT MSVC_VERSION LESS 1400)
		set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /bigobj")
	  endif()
	  if(BUILD_WITH_DEBUG_INFO)
		set(BUILD_EXTRA_FLAGS_RELEASE "${BUILD_EXTRA_FLAGS_RELEASE} /Zi")
	  endif()

	  if(NOT MSVC64)
		# 64-bit MSVC compiler uses SSE/SSE2 by default
		if(ENABLE_SSE)
		  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /arch:SSE")
		endif()
		if(ENABLE_SSE2)
		  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /arch:SSE2")
		endif()
	  endif()

	  if(ENABLE_AVX)
		set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /arch:AVX")
	  endif()

	  if(ENABLE_SSE OR ENABLE_SSE2 OR ENABLE_SSE3 OR ENABLE_SSE4_1)
		set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /Oi")
	  endif()

	  if(X86 OR X86_64)
		if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND ENABLE_SSE2)
		  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /fp:fast")# !! important - be on the same wave with x64 compilers
		endif()
	  endif()

	  # fix virtual memory range for PCH exceeded error
	  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /Zm170")

	  # enable __cplusplus
	  set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS} /Zc:__cplusplus")
	endif()

	# Extra link libs if the user selects building static libs:
	# Android does not need these settings because they are already set by toolchain file
	if(CMAKE_COMPILER_IS_GNUCXX AND NOT ANDROID AND NOT BUILD_SHARED_LIBS)
		set(BUILD_EXTRA_LINKER_LIBS "${BUILD_EXTRA_LINKER_LIBS} stdc++")
	endif()

	# Add user supplied extra options (optimization, etc...)
	# ==========================================================
	set(BUILD_EXTRA_FLAGS "${BUILD_EXTRA_FLAGS}" CACHE INTERNAL "Extra compiler options")
	set(BUILD_EXTRA_C_FLAGS "${BUILD_EXTRA_C_FLAGS}" CACHE INTERNAL "Extra compiler options for C sources")
	set(BUILD_EXTRA_CXX_FLAGS "${BUILD_EXTRA_CXX_FLAGS}" CACHE INTERNAL "Extra compiler options for C++ sources")
	set(BUILD_EXTRA_FLAGS_RELEASE "${BUILD_EXTRA_FLAGS_RELEASE}" CACHE INTERNAL "Extra compiler options for Release build")
	set(BUILD_EXTRA_FLAGS_DEBUG "${BUILD_EXTRA_FLAGS_DEBUG}" CACHE INTERNAL "Extra compiler options for Debug build")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS "${BUILD_EXTRA_EXE_LINKER_FLAGS}" CACHE INTERNAL "Extra linker flags")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE "${BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE}" CACHE INTERNAL "Extra linker flags for Release build")
	set(BUILD_EXTRA_EXE_LINKER_FLAGS_DEBUG "${BUILD_EXTRA_EXE_LINKER_FLAGS_DEBUG}" CACHE INTERNAL "Extra linker flags for Debug build")

	#combine all "extra" options
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${BUILD_EXTRA_FLAGS} ${BUILD_EXTRA_C_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${BUILD_EXTRA_FLAGS} ${BUILD_EXTRA_CXX_FLAGS}")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${BUILD_EXTRA_FLAGS_RELEASE}")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${BUILD_EXTRA_FLAGS_RELEASE}")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${BUILD_EXTRA_FLAGS_DEBUG}")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${BUILD_EXTRA_FLAGS_DEBUG}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${BUILD_EXTRA_EXE_LINKER_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${BUILD_EXTRA_EXE_LINKER_FLAGS_RELEASE}")
	set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${BUILD_EXTRA_EXE_LINKER_FLAGS_DEBUG}")

	if(MSVC)
	  # avoid warnings from MSVC about overriding the /W* option
	  # we replace /W3 with /W4 only for C++ files,
	  if(ENABLE_EXTRA_WARNINGS)
		  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
		  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
		  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	  endif()

	  if(NOT ENABLE_NOISY_WARNINGS AND MSVC_VERSION GREATER 1300)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4231") # disable warnings on extern before template instantiation
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251") # disable warnings on needs to have dll-interface to be used by clients of class ...
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4308") # for negative integral constant converted to unsigned type
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4396") # for ignored inlines
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4503") # for decorated name length exceeded, name was truncated
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4661") # disable warnings on no suitable definition provided for explicit template instantiation request
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996") # for deprecated functions
	  endif()

	  foreach(flags CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG)
		string(REPLACE "/Zm1000" "" ${flags} "${${flags}}")
	  endforeach()
	endif()
endmacro()


# Tweaks CMake's default compiler/linker settings to suit application's needs.
#
# This must be a macro(), as inside a function string() can only
# update variables in the function scope.
macro(fix_default_compiler_settings)
	if (MSVC)
		if (BUILD_STATIC_RUNTIME)
			# For MSVC, CMake sets certain flags to defaults we want to override.
			# This replacement code is taken from sample in the CMake Wiki at
			# http://www.cmake.org/Wiki/CMake_FAQ#Dynamic_Replace.
			foreach (flag_var
					 CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
					 CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
					 CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
					 CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
				# When the application is built as a shared library, it should also use
				# shared runtime libraries.  Otherwise, it may end up with multiple
				# copies of runtime library data in different modules, resulting in
				# hard-to-find crashes. When it is built as a static library, it is
				# preferable to use CRT as static libraries, as we don't have to rely
				# on CRT DLLs being available. CMake always defaults to using shared
				# CRT libraries, so we override that default here.
				string(REPLACE "/MD" "-MT" ${flag_var} "${${flag_var}}")
			endforeach()
		endif()
		# Set WholeProgramOptimization flags for release
		SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /GL")
		SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL")
		SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
		SET(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG")
	endif()
	# Save libs and executables in the same place
	SET(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/lib${PACKAGE_LIB_SUFFIX}" CACHE PATH "Output directory for libraries")
	SET(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin${PACKAGE_LIB_SUFFIX}" CACHE PATH "Output directory for executables")
	SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin${PACKAGE_LIB_SUFFIX}" CACHE PATH "Output directory for applications")
endmacro()

# Defines the compiler/linker flags used to build the application.
# You can tweak these definitions to suit your needs.  A
# variable's value is empty before it's explicitly assigned to.
macro(ConfigCompilerAndLinker)
  optimize_default_compiler_settings()
  fix_default_compiler_settings()
  if (MSVC)
    set(cxx_base_flags "-DWIN32 -D_WIN32")
    set(cxx_exception_flags "-EHsc -D_HAS_EXCEPTIONS=1")
    set(cxx_no_exception_flags "-D_HAS_EXCEPTIONS=0")
    set(cxx_no_rtti_flags "-GR-")
  elseif (CMAKE_COMPILER_IS_GNUCXX)
    set(cxx_base_flags "-Wall")
    set(cxx_exception_flags "-fexceptions")
    set(cxx_no_exception_flags "-fno-exceptions")
    # Until version 4.3.2, GCC doesn't define a macro to indicate
    # whether RTTI is enabled.  Therefore we define GTEST_HAS_RTTI
    # explicitly.
    set(cxx_no_rtti_flags "-fno-rtti -DGTEST_HAS_RTTI=0")
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "SunPro")
    set(cxx_exception_flags "-features=except")
    # Sun Pro doesn't provide macros to indicate whether exceptions and
    # RTTI are enabled, so we define GTEST_HAS_* explicitly.
    set(cxx_no_exception_flags "-features=no%except -DGTEST_HAS_EXCEPTIONS=0")
    set(cxx_no_rtti_flags "-features=no%rtti -DGTEST_HAS_RTTI=0")
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "VisualAge" OR CMAKE_CXX_COMPILER_ID STREQUAL "XL")
    # CMake 2.8 changes Visual Age's compiler ID to "XL".
    set(cxx_exception_flags "-qeh")
    set(cxx_no_exception_flags "-qnoeh")
    # Until version 9.0, Visual Age doesn't define a macro to indicate
    # whether RTTI is enabled.  Therefore we define GTEST_HAS_RTTI
    # explicitly.
    set(cxx_no_rtti_flags "-qnortti -DGTEST_HAS_RTTI=0")
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "HP")
    set(cxx_base_flags "-AA -mt")
    set(cxx_exception_flags "-DGTEST_HAS_EXCEPTIONS=1")
    set(cxx_no_exception_flags "+noeh -DGTEST_HAS_EXCEPTIONS=0")
    # RTTI can not be disabled in HP aCC compiler.
    set(cxx_no_rtti_flags "")
  endif()

  if (BUILD_EXCEPTIONS_ENABLED)
    set(cxx_exception_support "${CMAKE_CXX_FLAGS} ${cxx_base_flags} ${cxx_exception_flags}")
    set(_HAS_EXCEPTIONS TRUE)
  else()
    set(cxx_exception_support "${CMAKE_CXX_FLAGS} ${cxx_base_flags} ${cxx_no_exception_flags}")
  endif()

  if (BUILD_RTTI_ENABLED)
    set(cxx_rtti_support "")
    set(_HAS_RTTI TRUE)
  else()
    set(cxx_rtti_support "${cxx_no_rtti_flags}")
  endif()
  
  set(cxx_default "${cxx_exception_support} ${cxx_rtti_support}" CACHE PATH "Common compile CXX flags")
  set(c_default "${CMAKE_C_FLAGS} ${cxx_base_flags}" CACHE PATH "Common compile C flags")

  if(APPLE)
    # Mitigate CMake limitation, see: https://discourse.cmake.org/t/avoid-duplicate-linking-to-avoid-xcode-15-warnings/9084/10
    add_link_options(LINKER:-no_warn_duplicate_libraries)
  endif()
endmacro()

# Initialize variables needed for a library type project.
macro(ConfigLibrary)
	# Offer the user the choice of overriding the installation directories
	set(INSTALL_LIB_DIR "lib" CACHE PATH "Installation directory for libraries")
	set(INSTALL_BIN_DIR "bin" CACHE PATH "Installation directory for executables")
	set(INSTALL_INCLUDE_DIR "include" CACHE PATH "Installation directory for header files")
	if(WIN32 AND NOT CYGWIN)
		set(DEF_INSTALL_CMAKE_DIR "CMake")
	else()
		set(DEF_INSTALL_CMAKE_DIR "lib/cmake")
	endif()
	set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation directory for CMake files")
	# Make relative paths absolute (needed later on)
	foreach(p LIB BIN INCLUDE CMAKE)
		set(var INSTALL_${p}_DIR)
		set(varp INSTALL_${p}_DIR_PREFIX)
		if(IS_ABSOLUTE "${${varp}}")
			set(${varp} "${${varp}}")
		else()
			set(${varp} "${CMAKE_INSTALL_PREFIX}/${${var}}")
		endif()
		set(${var} "${${varp}}/${PROJECT_NAME}")
	endforeach()
endmacro()

function(create_rc_files name)
  # Create the manifest file
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" 
    "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>
    <assembly manifestVersion='1.0' xmlns='urn:schemas-microsoft-com:asm.v1'>
      <assemblyIdentity type='win32' name='${name}' version='1.0.0.0'/>
      <trustInfo xmlns='urn:schemas-microsoft-com:asm.v2'>
        <security>
          <requestedPrivileges xmlns='urn:schemas-microsoft-com:asm.v3'>
            <requestedExecutionLevel level='asInvoker' uiAccess='false'/>
          </requestedPrivileges>
        </security>
      </trustInfo>
      <compatibility xmlns='urn:schemas-microsoft-com:compatibility.v1'>
        <application>
          <!-- Windows Vista -->
          <supportedOS Id='{e2011457-1546-43c5-a5fe-008deee3d3f0}'/>
          <!-- Windows 7 -->
          <supportedOS Id='{35138b9a-5d96-4fbd-8e2d-a2440225f93a}'/>
          <!-- Windows 8 -->
          <supportedOS Id='{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}'/>
          <!-- Windows 8.1 -->
          <supportedOS Id='{1f676c76-80e1-4239-95bb-83d0f6d0da78}'/>
          <!-- Windows 10 and Windows 11 -->
          <supportedOS Id='{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}'/>
        </application>
      </compatibility>
    </assembly>
  ")

  # Create an RC file that includes the manifest
  if(NOT "${ARGN}" STREQUAL "")
    set(RC_ICO "
      #define IDI_ICON_APP 101
      IDI_ICON_APP ICON DISCARDABLE \"${ARGN}\"
	")
  endif()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    "#include <windows.h>
    1 RT_MANIFEST \"app.manifest\"
    ${RC_ICO}
  ")
endfunction()

# Defines the main libraries.  User tests should link
# with one of them.
function(cxx_library_with_type name folder type cxx_flags)
  # type can be either STATIC or SHARED to denote a static or shared library.
  # ARGN refers to additional arguments after 'cxx_flags'.
  add_library("${name}" ${type} ${ARGN})
  #set_target_properties("${name}" PROPERTIES COMPILE_FLAGS "${cxx_flags}")
  if ((BUILD_SHARED_LIBS AND NOT type STREQUAL "STATIC") OR type STREQUAL "SHARED")
    set_target_properties("${name}" PROPERTIES COMPILE_DEFINITIONS "_USRDLL")
  else()
    set_target_properties("${name}" PROPERTIES COMPILE_DEFINITIONS "_LIB")
  endif()
  # Set project folder
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if(BUILD_SHARED_LIBS OR PARTIAL_BUILD_SHARED_LIBS)
    set_target_properties("${name}" PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endfunction()

# cxx_executable_with_flags(name cxx_flags libs srcs...)
#
# creates a named C++ executable that depends on the given libraries and
# is built from the given source files with the given compiler flags.
function(cxx_executable_with_flags name folder cxx_flags libs)
  add_executable("${name}" ${ARGN})
  if (cxx_flags)
    set_target_properties("${name}" PROPERTIES COMPILE_FLAGS "${cxx_flags}")
  endif()
  # To support mixing linking in static and dynamic libraries, link each
  # library in with an extra call to target_link_libraries.
  foreach (lib "${libs}")
    target_link_libraries("${name}" ${lib})
  endforeach()
  # Set project folder
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if (MSVC)
    # Check if any of the files listed in ARGN has the extension .rc
    foreach (file ${ARGN})
      if (file MATCHES "\\.rc$")
        set_target_properties("${name}" PROPERTIES LINK_FLAGS "/MANIFEST:NO")
        break()
      endif()
    endforeach()
  endif()
endfunction()