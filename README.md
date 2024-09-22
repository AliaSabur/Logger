# Logger
A high-performance, lock-free logging class in VC++.

## Compilation

Please use a compiler that supports the **C++20** standard.

# Example usage

```cpp
// Example usage:

// main.cpp
#include "logger.h"

int main() {
    // Create multiple Logger instances
    Logger logger1;
    if (!logger1.Init(LOG_DEBUG, "logs1", "app1", ROTATE_DAILY)) {
        fprintf(stderr, "Failed to initialize logger1.\n");
        return 1;
    }

    Logger logger2;
    if (!logger2.Init(LOG_INFO, "logs2", "app2", ROTATE_HOURLY)) {
        fprintf(stderr, "Failed to initialize logger2.\n");
        return 1;
    }

    // Log messages with different loggers
    logger1.debug(L"Logger1 Debug message: %d", 42);
    logger1.info(L"Logger1 Info message: %s", L"Sample Info");
    logger1.warn(L"Logger1 Warning message");
    logger1.error(L"Logger1 Error message");

    logger2.debug(L"Logger2 Debug message: %d", 24); // Will not be logged due to log level
    logger2.info(L"Logger2 Info message: %s", L"Another Sample Info");
    logger2.warn(L"Logger2 Warning message");
    logger2.error(L"Logger2 Error message");

    // Allow some time for the background threads to process logs
    Sleep(100);

    // Logger destructors will be called automatically, cleaning up resources
    return 0;
}
```
