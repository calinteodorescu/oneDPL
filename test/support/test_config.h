// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file incorporates work covered by the following copyright and permission
// notice:
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef _TEST_config_H
#define _TEST_config_H

#define _PSTL_TEST_STRING(X) _PSTL_TEST_STRING_AUX(oneapi/dpl/X)
#define _PSTL_TEST_STRING_AUX(X) #X
//to support the optional including: <algorithm>, <memory>, <numeric> or <pstl/algorithm>, <pstl/memory>, <pstl/numeric>
#define _PSTL_TEST_HEADER(HEADER_ID) _PSTL_TEST_STRING(HEADER_ID)

#if defined(_MSC_VER) && defined(_DEBUG)
#define _SCL_SECURE_NO_WARNINGS //to prevent the compilation warning. Microsoft STL implementation has specific checking of an iterator range in DEBUG mode for the containers from the standard library.
#endif

// ICC 18 (Windows) has encountered an unexpected problem on some tests
#define _PSTL_ICC_18_VC141_TEST_SIMD_LAMBDA_RELEASE_BROKEN                                                            \
    (!_DEBUG && __INTEL_COMPILER >= 1800 && __INTEL_COMPILER < 1900 && _MSC_VER == 1910)
// ICC 18 doesn't vectorize the loop
#define _PSTL_ICC_18_TEST_EARLY_EXIT_MONOTONIC_RELEASE_BROKEN (!_DEBUG && __INTEL_COMPILER && __INTEL_COMPILER == 1800)
// clang 900.0.38 produces fatal error: error in backend: Section too large
#define _PSTL_CLANG_TEST_BIG_OBJ_DEBUG_32_BROKEN                                                                      \
    (__i386__ && PSTL_USE_DEBUG && __clang__ && _PSTL_CLANG_VERSION <= 90000)
// ICC 18 generates wrong result with omp simd early_exit
#define _PSTL_ICC_18_TEST_EARLY_EXIT_AVX_RELEASE_BROKEN                                                               \
    (!_DEBUG && __INTEL_COMPILER == 1800 && __AVX__ && !__AVX2__ && !__AVX512__)
// ICC 19 has encountered an unexpected problem: Segmentation violation signal raised
#define _PSTL_ICC_19_TEST_IS_PARTITIONED_RELEASE_BROKEN                                                               \
    (!PSTL_USE_DEBUG && (__linux__ || __APPLE__) && __INTEL_COMPILER == 1900)
// ICC 19 generates wrong result with UDS on Windows
#define _PSTL_ICC_19_TEST_SIMD_UDS_WINDOWS_RELEASE_BROKEN (__INTEL_COMPILER == 1900 && _MSC_VER && !_DEBUG)
// ICPC compiler generates wrong "openMP simd" code for a user defined scan operation(UDS) for MacOS, Linuxand Windows
#define _PSTL_ICC_TEST_SIMD_UDS_BROKEN                                                                                \
    (__INTEL_COMPILER && __INTEL_COMPILER_BUILD_DATE < 20211123)
// ICC 18,19 generate wrong result
#define _PSTL_ICC_18_19_TEST_SIMD_MONOTONIC_WINDOWS_RELEASE_BROKEN													  \
    ((__INTEL_COMPILER == 1800 || __INTEL_COMPILER == 1900) && _MSC_VER && !_DEBUG)
// ICC 18,19 generate wrong result with for_loop_strided and reverse iterators
#define _PSTL_ICC_18_19_TEST_REVERSE_ITERATOR_WITH_STRIDE_BROKEN                                                      \
    (__i386__ && (__INTEL_COMPILER == 1800 || __INTEL_COMPILER == 1900))
// VC14 uninitialized_fill with no policy has broken implementation
#define _PSTL_STD_UNINITIALIZED_FILL_BROKEN (_MSC_VER == 1900)
// GCC10 produces wrong answer calling exclusive_scan using vectorized polices
#define TEST_GCC10_EXCLUSIVE_SCAN_BROKEN (_GLIBCXX_RELEASE == 10)

#define _PSTL_SYCL_TEST_USM 1

// Enable test when the DPC++ backend is available
#if (defined(CL_SYCL_LANGUAGE_VERSION) || defined(SYCL_LANGUAGE_VERSION)) && (!defined(ONEDPL_USE_DPCPP_BACKEND) || ONEDPL_USE_DPCPP_BACKEND != 0)
#define TEST_DPCPP_BACKEND_PRESENT 1
#else
#define TEST_DPCPP_BACKEND_PRESENT 0
#endif

#ifdef __SYCL_UNNAMED_LAMBDA__
#define TEST_UNNAMED_LAMBDAS 1
#else
#define TEST_UNNAMED_LAMBDAS 0
#endif

// Enables full scope of testing
#ifndef TEST_LONG_RUN
#define TEST_LONG_RUN 0
#endif

// Enable test when the TBB backend is available
#define TEST_TBB_BACKEND_PRESENT (!defined(ONEDPL_USE_TBB_BACKEND) || ONEDPL_USE_TBB_BACKEND != 0)

// Check for C++ standard and standard library for the use of ranges API
#if !defined(_ENABLE_RANGES_TESTING)
#define _TEST_RANGES_FOR_CPP_17_DPCPP_BE_ONLY (__cplusplus >= 201703L && TEST_DPCPP_BACKEND_PRESENT)
#if defined(_GLIBCXX_RELEASE)
#    define _ENABLE_RANGES_TESTING (_TEST_RANGES_FOR_CPP_17_DPCPP_BE_ONLY && _GLIBCXX_RELEASE >= 8 && __GLIBCXX__ >= 20180502)
#elif defined(_LIBCPP_VERSION)
#    define _ENABLE_RANGES_TESTING (_TEST_RANGES_FOR_CPP_17_DPCPP_BE_ONLY && _LIBCPP_VERSION >= 7000)
#else
#    define _ENABLE_RANGES_TESTING (_TEST_RANGES_FOR_CPP_17_DPCPP_BE_ONLY)
#endif
#endif //!defined(_ENABLE_RANGES_TESTING)

// to prevent the compilation warning in clang
#ifdef __clang__
#define STR(X) #X
#define CLANG_DIAGNOSTIC_PUSH                                 _Pragma(STR(clang diagnostic push))
#define CLANG_DIAGNOSTIC_POP                                  _Pragma(STR(clang diagnostic pop))
#define CLANG_DIAGNOSTIC_IGNORED_AUTOLOGICAL_CONSTANT_COMPARE _Pragma(STR(clang diagnostic ignored "-Wtautological-constant-compare"))
#define CLANG_DIAGNOSTIC_IGNORED_LITERAL_RANGE                _Pragma(STR(clang diagnostic ignored "-Wliteral-range"))
#else
#define CLANG_DIAGNOSTIC_PUSH
#define CLANG_DIAGNOSTIC_POP
#define CLANG_DIAGNOSTIC_IGNORED_AUTOLOGICAL_CONSTANT_COMPARE
#define CLANG_DIAGNOSTIC_IGNORED_LITERAL_RANGE
#endif

#define TEST_HAS_NO_INT128

#define _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN (__GLIBCXX__ >= 7)

#define _PSTL_GLIBCXX_TEST_COMPLEX_POW_BROKEN _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN
#define _PSTL_GLIBCXX_TEST_COMPLEX_DIV_EQ_BROKEN _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN
#define _PSTL_GLIBCXX_TEST_COMPLEX_MINUS_EQ_BROKEN _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN
#define _PSTL_GLIBCXX_TEST_COMPLEX_PLUS_EQ_BROKEN _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN
#define _PSTL_GLIBCXX_TEST_COMPLEX_TIMES_EQ_BROKEN _PSTL_GLIBCXX_TEST_COMPLEX_BROKEN

#define _PSTL_MSVC_LESS_THAN_CPP20_COMPLEX_CONSTEXPR_BROKEN (_MSC_VER && __cplusplus <= 201703L)

#endif /* _TEST_config_H */
