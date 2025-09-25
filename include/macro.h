// (c) 2025 AutoAlg (autoalg.com).
// Author: chunzhi qu.
// SPDX-License-Identifier: MIT.

#ifndef EASY_CONTROL_INCLUDE_MACRO_H
#define EASY_CONTROL_INCLUDE_MACRO_H

#if !defined(EC_DEBUG) && !defined(EC_RELEASE)
#if defined(NDEBUG)
#define EC_RELEASE 1
#else
#define EC_DEBUG 1
#endif
#endif

#if defined(_MSC_VER)
#define EC_COMPILER_MSVC 1
#elif defined(__clang__)
#define EC_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define EC_COMPILER_GCC 1
#endif

#if defined(__cplusplus)
#define EC_INLINE inline
#else
#define EC_INLINE static inline
#endif

#if defined(EC_DEBUG)
#define EC_FORCE_INLINE EC_INLINE
#else
#if EC_COMPILER_MSVC
#define EC_FORCE_INLINE __forceinline
#elif EC_COMPILER_CLANG || EC_COMPILER_GCC
#define EC_FORCE_INLINE __attribute__((always_inline)) inline
#else
#define EC_FORCE_INLINE EC_INLINE
#endif
#endif

#if EC_COMPILER_MSVC
#define EC_NOINLINE __declspec(noinline)
#elif EC_COMPILER_CLANG || EC_COMPILER_GCC
#define EC_NOINLINE __attribute__((noinline))
#else
#define EC_NOINLINE
#endif

#if EC_COMPILER_CLANG || EC_COMPILER_GCC
#define EC_LIKELY(x) __builtin_expect(!!(x), 1)
#define EC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define EC_LIKELY(x) (x)
#define EC_UNLIKELY(x) (x)
#endif

#if EC_COMPILER_CLANG || EC_COMPILER_GCC
#define EC_PURE __attribute__((pure))
#define EC_CONST __attribute__((const))
#else
#define EC_PURE
#define EC_CONST
#endif

#endif  // EASY_CONTROL_INCLUDE_MACRO_H
