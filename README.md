# Logger
A high-performance, lock-free logging class in VC++.

# Example usage

```cpp
// Example usage:

// main.cpp
#include "logger.h"

int main() {
    // Get the logger instance
    Logger& myLogger = Logger::GetInstance();

    // Initialize the logger
    if (!myLogger.Init(LOG_DEBUG, "logs", "app", ROTATE_DAILY)) {
        fprintf(stderr, "Failed to initialize logger.\n");
        return 1;
    }

    // Log messages with different levels
    myLogger.debug(L"Debug message: %d", 42);
    myLogger.info(L"Info message: %s", L"Sample Info");
    myLogger.warn(L"Warning message");
    myLogger.error(L"Error message");

    // Log UTF-8 messages directly
    myLogger.info_utf8("UTF-8 Info message: %s", "Sample UTF-8 Info");

    // Allow some time for the background thread to process logs
    Sleep(100);

    return 0;
}
```
