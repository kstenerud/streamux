#pragma once

#ifndef STREAMUX_PUBLIC
    #if defined _WIN32 || defined __CYGWIN__
        #define STREAMUX_PUBLIC __declspec(dllimport)
    #else
        #define STREAMUX_PUBLIC
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

struct streamux_context;

typedef enum
{
    STREAMUX_STATUS_OK,
} streamux_status;

enum
{
    PRIORITY_MIN = 0,
    PRIORITY_MAX = 65534,
    PRIORITY_OOB = 65535,
};

// -----------
// Library API
// -----------

/**
 * Get the current library version as a semantic version (e.g. "1.5.2").
 *
 * @return The library version.
 */
STREAMUX_PUBLIC const char* streamux_version();





#ifdef __cplusplus 
}
#endif
