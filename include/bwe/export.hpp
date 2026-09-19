/// @file
/// @brief Definition of the BWE_API export macro.

#pragma once

#if defined(BWE_STATIC)
#define BWE_API
#elif defined(_WIN32)
#if defined(BWE_BUILDING_LIBRARY)
#define BWE_API __declspec(dllexport)
#else
#define BWE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define BWE_API __attribute__((visibility("default")))
#else
#define BWE_API
#endif
