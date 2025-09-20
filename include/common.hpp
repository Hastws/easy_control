// (c) 2025 AutoAlg (autoalg.com). 
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifndef EASY_CONTROL_INCLUDE_APPLE_COMMON_H
#define EASY_CONTROL_INCLUDE_APPLE_COMMON_H

#ifdef __APPLE__

#include <unistd.h>

namespace autoalg {
    inline void Sleep(const unsigned int value) {
        sleep(value);
    }
}

#endif

#endif // EASY_CONTROL_INCLUDE_APPLE_COMMON_H
