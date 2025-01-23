#pragma once

#include <windows.h>
#include <atomic>
#include <vector>
#include <memory>
#include <string>
#include <cstdio>
#include <mutex>
#include <cmath> // std::abs

namespace loggingutils {

    // Log levels
    enum Level {
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR
    };

    // Log rotation strategies
    enum RotationKind {
        ROTATE_MINUTELY,
        ROTATE_HOURLY,
        ROTATE_DAILY,
        ROTATE_NEVER
    };

    class Logger {
    private:
        // Single log entry in a circular buffer
        struct LogEntry {
            std::atomic<bool> ready{ false };
            std::string message; // UTF-8 encoded string
        };

        // Buffer capacity
        static constexpr size_t BUFFER_SIZE = 1024;

        // Logging settings
        Level level;
        char logDir[MAX_PATH];
        char logFileName[MAX_PATH];
        RotationKind rotationKind;

        // Stored local time for log rotation checking
        SYSTEMTIME lastRotationTime;

        // Log file handle
        HANDLE hFile;

        // Atomic indexes for a lock-free circular buffer
        std::atomic<size_t> head{ 0 };
        std::atomic<size_t> tail{ 0 };

        // The log entries buffer
        std::vector<LogEntry> buffer;

        // Worker thread handle
        HANDLE logThread;

        // Indicates if the logger is running
        std::atomic<bool> running{ false };

        // Mutex for writing to the file
        std::mutex fileMutex;

        // Background thread function
        static inline DWORD WINAPI ThreadFunc(void* data);

        // Internal helper functions
        inline void processLogQueue();
        inline void checkRotation();
        inline void openLogFile();
        inline void generateLogFileName(char* fullPath, RotationKind rotationKind, const SYSTEMTIME& time);
        inline const char* levelToString(Level level) const;
        inline std::string ConvertToUTF8(const wchar_t* message);

        // Build an RFC3339-formatted local time string, including time zone offset
        inline std::string currentLocalTimeRFC3339() const;

    public:
        // Constructor
        inline Logger();

        // Disable copy constructor and assignment
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        // Initialization: can be called multiple times; it will call finalize() first if already running
        inline bool init(Level lvl, const char* logDirectory, const char* logFileNamePrefix, RotationKind rotation);

        // Manual cleanup: stops the thread, flushes and closes the file
        inline void finalize();

        // Destructor (automatically calls finalize() if not called)
        inline ~Logger();

        // Log a UTF-8 string directly
        inline void log(Level msgLevel, const char* message);

        // Log a formatted wide-character string (converted to UTF-8)
        template<typename... Args>
        inline void logf(Level msgLevel, const wchar_t* format, Args... args);

        // Log a formatted UTF-8 string
        template<typename... Args>
        inline void logf_utf8(Level msgLevel, const char* format, Args... args);

        // Helper functions for different levels
        template<typename... Args>
        inline void debug(const wchar_t* format, Args... args);
        template<typename... Args>
        inline void debug_utf8(const char* format, Args... args);

        template<typename... Args>
        inline void info(const wchar_t* format, Args... args);
        template<typename... Args>
        inline void info_utf8(const char* format, Args... args);

        template<typename... Args>
        inline void warn(const wchar_t* format, Args... args);
        template<typename... Args>
        inline void warn_utf8(const char* format, Args... args);

        template<typename... Args>
        inline void error(const wchar_t* format, Args... args);
        template<typename... Args>
        inline void error_utf8(const char* format, Args... args);
    };

    //---------------------
    // Implementations
    //---------------------

    inline Logger::Logger()
        : buffer(BUFFER_SIZE),
        hFile(INVALID_HANDLE_VALUE),
        logThread(NULL)
    {
        level = LOG_DEBUG;
        rotationKind = ROTATE_NEVER;
        memset(logDir, 0, sizeof(logDir));
        memset(logFileName, 0, sizeof(logFileName));
        GetLocalTime(&lastRotationTime);
    }

    inline bool Logger::init(Level lvl, const char* logDirectory, const char* logFileNamePrefix, RotationKind rotation) {
        // If already running, finalize it to allow re-init
        if (running) {
            finalize();
        }
        level = lvl;
        rotationKind = rotation;
        strncpy_s(logDir, _countof(logDir), logDirectory, _TRUNCATE);
        strncpy_s(logFileName, _countof(logFileName), logFileNamePrefix, _TRUNCATE);
        GetLocalTime(&lastRotationTime);

        hFile = INVALID_HANDLE_VALUE;
        openLogFile();

        // Start background thread
        running = true;
        logThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
        if (logThread == NULL) {
            fprintf(stderr, "[Logger] Failed to create log thread.\n");
            running = false;
            return false;
        }
        return true;
    }

    inline void Logger::finalize() {
        // If already stopped, return
        bool wasRunning = running.exchange(false, std::memory_order_acq_rel);
        if (!wasRunning) {
            return;
        }
        // Wait for worker thread to exit
        if (logThread != NULL) {
            WaitForSingleObject(logThread, INFINITE);
            CloseHandle(logThread);
            logThread = NULL;
        }
        // Close file
        if (hFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
    }

    inline Logger::~Logger() {
        finalize();
    }

    inline DWORD WINAPI Logger::ThreadFunc(void* data) {
        Logger* logger = static_cast<Logger*>(data);
        logger->processLogQueue();
        return 0;
    }

    inline void Logger::processLogQueue() {
        while (running.load(std::memory_order_acquire)) {
            auto currentTail = tail.load(std::memory_order_acquire);
            while (currentTail != head.load(std::memory_order_acquire)) {
                if (buffer[currentTail].ready.load(std::memory_order_acquire)) {
                    checkRotation();
                    {
                        std::lock_guard<std::mutex> lock(fileMutex);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            DWORD written = 0;
                            WriteFile(
                                hFile,
                                buffer[currentTail].message.c_str(),
                                static_cast<DWORD>(buffer[currentTail].message.size()),
                                &written,
                                NULL
                            );
                        }
                    }
                    // Mark this entry as processed
                    buffer[currentTail].ready.store(false, std::memory_order_release);
                    currentTail = (currentTail + 1) % BUFFER_SIZE;
                    tail.store(currentTail, std::memory_order_release);
                }
            }
            // Sleep to reduce busy waiting
            Sleep(10);
        }
        // Before thread exits, flush and close file
        std::lock_guard<std::mutex> lock(fileMutex);
        if (hFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
    }

    inline void Logger::checkRotation() {
        if (rotationKind == ROTATE_NEVER) {
            return;
        }
        SYSTEMTIME currentTime;
        GetLocalTime(&currentTime);

        bool shouldRotate = false;
        switch (rotationKind) {
        case ROTATE_MINUTELY:
            if (currentTime.wMinute != lastRotationTime.wMinute ||
                currentTime.wHour != lastRotationTime.wHour ||
                currentTime.wDay != lastRotationTime.wDay ||
                currentTime.wMonth != lastRotationTime.wMonth ||
                currentTime.wYear != lastRotationTime.wYear)
            {
                shouldRotate = true;
            }
            break;
        case ROTATE_HOURLY:
            if (currentTime.wHour != lastRotationTime.wHour ||
                currentTime.wDay != lastRotationTime.wDay ||
                currentTime.wMonth != lastRotationTime.wMonth ||
                currentTime.wYear != lastRotationTime.wYear)
            {
                shouldRotate = true;
            }
            break;
        case ROTATE_DAILY:
            if (currentTime.wDay != lastRotationTime.wDay ||
                currentTime.wMonth != lastRotationTime.wMonth ||
                currentTime.wYear != lastRotationTime.wYear)
            {
                shouldRotate = true;
            }
            break;
        default:
            break;
        }

        if (shouldRotate) {
            std::lock_guard<std::mutex> lock(fileMutex);
            if (hFile != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(hFile);
                CloseHandle(hFile);
                hFile = INVALID_HANDLE_VALUE;
            }
            lastRotationTime = currentTime;
            openLogFile();
        }
    }

    inline void Logger::openLogFile() {
        char fullPath[MAX_PATH * 2];
        generateLogFileName(fullPath, rotationKind, lastRotationTime);

        CreateDirectoryA(logDir, NULL);

        hFile = CreateFileA(
            fullPath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[Logger] Failed to open log file: %s\n", fullPath);
        }
        else {
            SetFilePointer(hFile, 0, NULL, FILE_END);
        }
    }

    inline void Logger::generateLogFileName(char* fullPath, RotationKind rotationKind, const SYSTEMTIME& time) {
        char timestamp[64];
        switch (rotationKind) {
        case ROTATE_MINUTELY:
            sprintf_s(timestamp, "%04u%02u%02u_%02u%02u",
                time.wYear, time.wMonth, time.wDay,
                time.wHour, time.wMinute);
            break;
        case ROTATE_HOURLY:
            sprintf_s(timestamp, "%04u%02u%02u_%02u",
                time.wYear, time.wMonth, time.wDay,
                time.wHour);
            break;
        case ROTATE_DAILY:
            sprintf_s(timestamp, "%04u%02u%02u",
                time.wYear, time.wMonth, time.wDay);
            break;
        case ROTATE_NEVER:
        default:
            strcpy_s(timestamp, "");
            break;
        }

        if (rotationKind != ROTATE_NEVER) {
            sprintf_s(fullPath, MAX_PATH * 2, "%s\\%s_%s.log", logDir, logFileName, timestamp);
        }
        else {
            sprintf_s(fullPath, MAX_PATH * 2, "%s\\%s.log", logDir, logFileName);
        }
    }

    inline const char* Logger::levelToString(Level lvl) const {
        switch (lvl) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        }
        return "UNKNOWN";
    }

    inline std::string Logger::ConvertToUTF8(const wchar_t* message) {
        if (!message) {
            return "";
        }
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
        if (sizeNeeded <= 0) {
            return "";
        }
        std::string utf8Str(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message, -1, &utf8Str[0], sizeNeeded, nullptr, nullptr);
        if (!utf8Str.empty()) {
            utf8Str.pop_back();
        }
        return utf8Str;
    }

    inline std::string Logger::currentLocalTimeRFC3339() const {
        SYSTEMTIME stLocal;
        GetLocalTime(&stLocal);

        TIME_ZONE_INFORMATION tzInfo;
        DWORD tzState = GetTimeZoneInformation(&tzInfo);

        // The Bias field is in minutes, negative means UTC+ offset
        LONG totalBias = tzInfo.Bias;
        if (tzState == TIME_ZONE_ID_DAYLIGHT) {
            totalBias += tzInfo.DaylightBias;
        }
        else if (tzState == TIME_ZONE_ID_STANDARD) {
            totalBias += tzInfo.StandardBias;
        }

        LONG offsetMinutes = -totalBias;
        LONG offsetHours = offsetMinutes / 60;
        LONG offsetMins = offsetMinutes % 60;

        LONG absOffsetHours = std::abs(offsetHours);
        LONG absOffsetMins = std::abs(offsetMins);

        char buffer[128] = { 0 };
        sprintf_s(
            buffer,
            "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02u:%02u",
            stLocal.wYear,
            stLocal.wMonth,
            stLocal.wDay,
            stLocal.wHour,
            stLocal.wMinute,
            stLocal.wSecond,
            stLocal.wMilliseconds,
            (offsetHours >= 0) ? '+' : '-',
            absOffsetHours,
            absOffsetMins
        );
        return buffer;
    }

    inline void Logger::log(Level msgLevel, const char* message) {
        if (msgLevel < level || !running) {
            return;
        }
        std::string utf8Msg(message ? message : "");

        // Acquire next writing position in the circular buffer
        auto nextHead = (head.load(std::memory_order_relaxed) + 1) % BUFFER_SIZE;
        while (nextHead == tail.load(std::memory_order_acquire)) {
            // If the buffer is full, block temporarily
            Sleep(1);
        }

        // Build the final log message with timestamp
        std::string timeStr = currentLocalTimeRFC3339();
        std::string formatted = timeStr + " [" + levelToString(msgLevel) + "] " + utf8Msg + "\n";

        buffer[head].message = formatted;
        buffer[head].ready.store(true, std::memory_order_release);
        head.store(nextHead, std::memory_order_release);
    }

    template<typename... Args>
    inline void Logger::logf(Level msgLevel, const wchar_t* format, Args... args) {
        if (msgLevel < level || !running) {
            return;
        }
        int sizeNeeded = _scwprintf(format, args...) + 1;
        std::unique_ptr<wchar_t[]> wideBuf(new wchar_t[sizeNeeded]);
        swprintf_s(wideBuf.get(), sizeNeeded, format, args...);

        log(msgLevel, ConvertToUTF8(wideBuf.get()).c_str());
    }

    template<typename... Args>
    inline void Logger::logf_utf8(Level msgLevel, const char* format, Args... args) {
        if (msgLevel < level || !running) {
            return;
        }
        int sizeNeeded = _scprintf(format, args...) + 1;
        std::unique_ptr<char[]> buf(new char[sizeNeeded]);
        sprintf_s(buf.get(), sizeNeeded, format, args...);

        log(msgLevel, buf.get());
    }

    template<typename... Args>
    inline void Logger::debug(const wchar_t* format, Args... args) {
        logf(LOG_DEBUG, format, args...);
    }
    template<typename... Args>
    inline void Logger::debug_utf8(const char* format, Args... args) {
        logf_utf8(LOG_DEBUG, format, args...);
    }

    template<typename... Args>
    inline void Logger::info(const wchar_t* format, Args... args) {
        logf(LOG_INFO, format, args...);
    }
    template<typename... Args>
    inline void Logger::info_utf8(const char* format, Args... args) {
        logf_utf8(LOG_INFO, format, args...);
    }

    template<typename... Args>
    inline void Logger::warn(const wchar_t* format, Args... args) {
        logf(LOG_WARN, format, args...);
    }
    template<typename... Args>
    inline void Logger::warn_utf8(const char* format, Args... args) {
        logf_utf8(LOG_WARN, format, args...);
    }

    template<typename... Args>
    inline void Logger::error(const wchar_t* format, Args... args) {
        logf(LOG_ERROR, format, args...);
    }
    template<typename... Args>
    inline void Logger::error_utf8(const char* format, Args... args) {
        logf_utf8(LOG_ERROR, format, args...);
    }

} // namespace loggingutils
