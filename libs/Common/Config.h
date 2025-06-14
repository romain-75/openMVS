////////////////////////////////////////////////////////////////////
// Config.h
//
// Copyright 2007 cDc@seacave
// Distributed under the Boost Software License, Version 1.0
// (See http://www.boost.org/LICENSE_1_0.txt)

#ifndef __SEACAVE_CONFIG_H__
#define __SEACAVE_CONFIG_H__

// Configure everything that needs to be globally known

#include "ConfigLocal.h"


// D E F I N E S ///////////////////////////////////////////////////

#ifdef _MSC_VER

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#if _MSC_VER > 1400
#ifndef NTDDI_VERSION		// There's now just one symbol to specify the minimum target operating system.
#define NTDDI_VERSION NTDDI_WIN7 // All the other symbols are set automatically to the appropriate values for the target operating system.
#endif
#else
#ifndef WINVER				// Allow use of features specific to Windows 95 and Windows NT 4 or later.
#define WINVER 0x0500		// Change this to the appropriate value to target Windows 98 and Windows 2000 or later.
#endif
#ifndef _WIN32_WINNT		// Allow use of features specific to Windows NT 4 or later.
#define _WIN32_WINNT 0x0500	// Change this to the appropriate value to target Windows 98 and Windows 2000 or later.
#endif
#ifndef _WIN32_WINDOWS		// Allow use of features specific to Windows 98 or later.
#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
#endif
#ifndef _WIN32_IE			// Allow use of features specific to IE 4.0 or later.
#define _WIN32_IE 0x0501	// Change this to the appropriate value to target IE 5.0 or later.
#endif
#endif

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers
#endif

#ifndef STRICT
#define STRICT
#endif

#define WIN32_MEAN_AND_LEAN

#define NOMINMAX

#define _USE_MATH_DEFINES

#if _MSC_VER >= 1400
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE 1
#endif
#ifndef _ATL_SECURE_NO_DEPRECATE
#define _ATL_SECURE_NO_DEPRECATE 1
#endif
#ifndef _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NON_CONFORMING_SWPRINTFS 1
#endif
#ifndef _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES 0 // disable automatically overloading CPP names to secure versions
#endif
#if 0 && defined(_DEBUG) && !defined(_ITERATOR_DEBUG_LEVEL) // might not build if linking statically to 3rd party libraries
#define _ITERATOR_DEBUG_LEVEL 1 // disable std iterator debugging even in Debug, as it is very slow
#endif
#endif


//----------------------------------------------------------------------
// For Microsoft Visual C++, externally accessible symbols must be
// explicitly indicated with DLL_API.
//
// The following ifdef block is the standard way of creating macros
// which make exporting from a DLL simpler. All files within this DLL
// are compiled with the DLL_EXPORTS preprocessor symbol defined on the
// command line. In contrast, projects that use (or import) the DLL
// objects do not define the DLL_EXPORTS symbol. This way any other
// project whose source files include this file see DLL_API functions as
// being imported from a DLL, whereas this DLL sees symbols defined with
// this macro as being exported.
//----------------------------------------------------------------------
#define EXPORT_API __declspec(dllexport)
#define IMPORT_API __declspec(dllimport)
/*----------------------------------------------------------------*/
#ifdef _USRDLL
  #ifdef Common_EXPORTS
	#define GENERAL_API EXPORT_API
	#define GENERAL_TPL
  #else
	#define GENERAL_API IMPORT_API
	#define GENERAL_TPL extern
  #endif
#else
  #define GENERAL_API
  #define GENERAL_TPL
#endif
/*----------------------------------------------------------------*/

// Define platform type
#if _WIN64
#define _ENVIRONMENT64
#else
#define _ENVIRONMENT32
#endif

#else // _MSC_VER

#if !defined(_DEBUG) && !defined(NDEBUG)
#define _DEBUG
#endif

//----------------------------------------------------------------------
// DLL_API is ignored for all other systems
//----------------------------------------------------------------------
#define EXPORT_API
#define IMPORT_API
#define GENERAL_API
#define GENERAL_TPL

// Define platform type
#if defined(__x86_64__) || defined(__ppc64__) || defined(__aarch64__) || defined(__arm64__) || defined(__mips64)
#define _ENVIRONMENT64
#else
#define _ENVIRONMENT32
#endif

#endif // _MSC_VER


#if __cplusplus >= 201103L || (__clang_major__ >= 4 || (__clang_major__ >= 3 && __clang_minor__ >= 3))
#define _SUPPORT_CPP11
#endif
#if __cplusplus >= 201402L || (__clang_major__ >= 4 || (__clang_major__ >= 3 && __clang_minor__ >= 4))
#define _SUPPORT_CPP14
#endif
#if __cplusplus >= 201703L || __clang_major__ >= 5
#define _SUPPORT_CPP17
#endif
#if __cplusplus >= 202002L || __clang_major__ >= 10
#define _SUPPORT_CPP20
#endif
#if __cplusplus >= 202302L || __clang_major__ >= 16
#define _SUPPORT_CPP23
#endif


#if defined(__powerpc__)
#define _PLATFORM_PPC 1
#elif defined(__arm__) || defined (__arm64__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARMT)
#define _PLATFORM_ARM 1
#else
#define _PLATFORM_X86 1
#endif


#if !defined(_DEBUG) && !defined(_PROFILE)
#define _RELEASE // exclude code useful only for debug
#endif


// optimization flags
#if defined(_MSC_VER)
#	define ALIGN(n) __declspec(align(n))
#	define NOINITVTABLE __declspec(novtable) //disable generating code to initialize the vfptr in the constructor(s) and destructor of the class
#	define DECRESTRICT __declspec(restrict) //applied to a function declaration or definition that returns a pointer type and tells the compiler that the function returns an object that will not be aliased with any other pointers
#	define NOALIAS __declspec(noalias) //applied to a function declaration or definition that returns a pointer type and tells the compiler that the function call does not modify or reference visible global state and only modifies the memory pointed to directly by pointer parameters (first-level indirections)
#	define RESTRICT  __restrict //applied to a function parameter
#	define MEMALLOC __declspec(noalias) __declspec(restrict)
#	define DEPRECATED __declspec(deprecated)
#	define MAYBEUNUSED
#	define HOT
#	define COLD
#	define THREADLOCAL __declspec(thread)
#	define FORCEINLINE __forceinline
#elif defined(__GNUC__)
#	define ALIGN(n) __attribute__((aligned(n)))
#	define NOINITVTABLE
#	define DECRESTRICT
#	define NOALIAS
#	define RESTRICT  __restrict__
#	define MEMALLOC __attribute__ ((__malloc__))
#	define DEPRECATED __attribute__ ((__deprecated__))
#	define MAYBEUNUSED __attribute__ ((unused))
#	define HOT __attribute__((hot)) __attribute__((optimize("-O3"))) __attribute__((optimize("-ffast-math"))) //optimize for speed, even in debug
#	define COLD __attribute__((cold)) //optimize for size
#	define THREADLOCAL __thread
#	define FORCEINLINE inline //__attribute__((always_inline))
#else
#	define ALIGN(n)
#	define NOINITVTABLE
#	define DECRESTRICT
#	define NOALIAS
#	define RESTRICT
#	define MEMALLOC
#	define DEPRECATED
#	define MAYBEUNUSED
#	define HOT
#	define COLD
#	define THREADLOCAL __thread
#	define FORCEINLINE inline
#endif

#ifdef _SUPPORT_CPP17
#	undef MAYBEUNUSED
#	define MAYBEUNUSED [[maybe_unused]]
#endif

#define SAFE_DELETE(p)		{ if (p!=NULL) { delete (p);     (p)=NULL; } }
#define SAFE_DELETE_ARR(p)	{ if (p!=NULL) { delete [] (p);  (p)=NULL; } }
#define SAFE_FREE(p)		{ if (p!=NULL) { free(p);        (p)=NULL; } }
#define SAFE_RELEASE(p)		{ if (p!=NULL) { (p)->Release(); (p)=NULL; } }


#ifdef _MSC_VER
#	define DEBUG_BREAK __debugbreak
#else
#if defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
#	define DEBUG_BREAK __builtin_debugtrap
#else
# if defined(__i386__) || defined(__x86_64__)
__inline__ static void trap_instruction() { __asm__ volatile("int $3"); }
#	define DEBUG_BREAK trap_instruction
# elif defined(__arm__)
__attribute__((always_inline))
__inline__ static void trap_instruction() { __asm__ volatile("bkpt #0"); }
#	define DEBUG_BREAK trap_instruction
# elif defined(__aarch64__)
__attribute__((always_inline))
__inline__ static void trap_instruction() { __asm__ volatile("brk #0"); }
#	define DEBUG_BREAK trap_instruction
# else
#	define DEBUG_BREAK __builtin_trap
# endif
#endif
#endif

#define PRINT_ASSERT_MSG(exp, ...)

#ifdef _DEBUG

#ifdef _MSC_VER
#define _DEBUGINFO
#define _CRTDBG_MAP_ALLOC //enable this to show also the filename (DEBUG_NEW should also be defined in each file)
#include <cstdlib>
#include <crtdbg.h>
#ifdef _INC_CRTDBG
#define SIMPLE_ASSERT(exp) {if (!(exp) && 1 == _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, #exp)) _CrtDbgBreak();}
#define ASSERT(exp, ...) {static bool bIgnore(false); if (!bIgnore && !(exp)) {PRINT_ASSERT_MSG(exp, ##__VA_ARGS__); if (!(bIgnore = !(1 == _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, #exp)))) _CrtDbgBreak();}}
#else
#define SIMPLE_ASSERT(exp) {if (!(exp)) DEBUG_BREAK();}
#define ASSERT(exp, ...) {if (!(exp)) {PRINT_ASSERT_MSG(exp, ##__VA_ARGS__); DEBUG_BREAK();}}
#endif // _INC_CRTDBG
#define TRACE(...) {TCHAR buffer[2048]; _sntprintf(buffer, 2048, __VA_ARGS__); OutputDebugString(buffer);}
#else // _MSC_VER
#include <assert.h>
#define SIMPLE_ASSERT(exp) {if (!(exp)) DEBUG_BREAK();}
#define ASSERT(exp, ...) {if (!(exp)) {PRINT_ASSERT_MSG(exp, ##__VA_ARGS__); DEBUG_BREAK();}}
#define TRACE(...)
#endif // _MSC_VER

#else

#ifdef _RELEASE
#define SIMPLE_ASSERT(exp)
#define ASSERT(exp, ...)
#else
#ifdef _MSC_VER
#define SIMPLE_ASSERT(exp) {if (!(exp)) __debugbreak();}
#define ASSERT(exp, ...) {if (!(exp)) {PRINT_ASSERT_MSG(exp, ##__VA_ARGS__); __debugbreak();}}
#else // _MSC_VER
#define SIMPLE_ASSERT(exp) {if (!(exp)) __builtin_trap();}
#define ASSERT(exp, ...) {if (!(exp)) {PRINT_ASSERT_MSG(exp, ##__VA_ARGS__); __builtin_trap();}}
#endif // _MSC_VER
#endif
#define TRACE(...)

#endif // _DEBUG


namespace SEACAVE_ASSERT
{
	template <bool value> struct compile_time_assert;
	template <> struct compile_time_assert<true> { enum {value=1}; };

	template <typename T, typename U> struct assert_are_same_type;
	template <typename T> struct assert_are_same_type<T,T> { enum{value=1}; };

	template <typename T, typename U> struct assert_are_not_same_type { enum{value=1}; };
	template <typename T> struct assert_are_not_same_type<T,T> {};
}

#define STATIC_ASSERT(expression) \
	MAYBEUNUSED typedef char CTA##__LINE__[::SEACAVE_ASSERT::compile_time_assert<(bool)(expression)>::value]

#define ASSERT_ARE_SAME_TYPE(type1, type2) \
	MAYBEUNUSED typedef char AAST##__LINE__[::SEACAVE_ASSERT::assert_are_same_type<type1,type2>::value]

#define ASSERT_ARE_NOT_SAME_TYPE(type1, type2) \
	MAYBEUNUSED typedef char AANST##__LINE__[::SEACAVE_ASSERT::assert_are_not_same_type<type1,type2>::value]
/*----------------------------------------------------------------*/

#endif // __SEACAVE_CONFIG_H__
