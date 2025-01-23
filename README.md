# Logger
A high-performance, lock-free logging class in VC++.

## Compilation

Please use a compiler that supports the **C++20** standard.

# Example usage

```cpp
// Example usage:

// main.cpp
#include <windows.h>
#include <cstdio>
#include "logger.h"

int main() {
    loggingutils::Logger logger;

    // Initialize the logger with the desired level, directory, file prefix, and rotation strategy
    // This example logs INFO/WARN/ERROR (DEBUG is ignored) and rotates daily
    if (!logger.Init(loggingutils::LOG_INFO, "logs", "myapp", loggingutils::ROTATE_DAILY)) {
        fprintf(stderr, "Failed to initialize logger.\n");
        return -1;
    }

    // Log some messages
    logger.info(L"Hello, %ls!", L"world");
    logger.debug_utf8("Debug message (this won't appear, because the level is INFO)");
    logger.warn_utf8("Warning example: code=%d", 123);
    logger.error(L"Some error occurred: code=%d", 999);

    // Optionally, manually finalize
    // If not called, the destructor will automatically finalize the logger
    logger.finalize();

    return 0;
}
```
