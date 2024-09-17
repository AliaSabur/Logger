// logger.h
#pragma once

#include <windows.h>
#include <atomic>
#include <vector>
#include <memory>
#include <string>
#include <cstdio>
#include <mutex>

enum Level {
	LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR
};

enum RotationKind {
	ROTATE_MINUTELY,
	ROTATE_HOURLY,
	ROTATE_DAILY,
	ROTATE_NEVER
};

class Logger {
private:
	struct LogEntry {
		std::atomic<bool> ready{ false };
		std::string message; // UTF-8 encoded strings
	};

	static constexpr size_t BUFFER_SIZE = 1024;
	Level level;
	char logDir[MAX_PATH];
	char logFileName[MAX_PATH];
	RotationKind rotationKind;
	SYSTEMTIME lastRotationTime;
	HANDLE hFile;
	std::atomic<size_t> head{ 0 }, tail{ 0 };
	std::vector<LogEntry> buffer;
	HANDLE logThread;
	std::atomic<bool> running{ true };
	std::mutex fileMutex;

	// Private constructor to prevent direct instantiation
	Logger();

	// Deleted copy constructor and assignment operator to prevent copying
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	static DWORD WINAPI ThreadFunc(void* data);
	const char* levelToString(Level level) const;
	void processLogQueue();
	void checkRotation();
	void openLogFile();
	void generateLogFileName(char* fullPath, RotationKind rotationKind, const SYSTEMTIME& time);
	std::string ConvertToUTF8(const wchar_t* message);
	std::string currentTime() const;

public:
	// Static method to get the singleton instance
	static Logger& GetInstance();

	// Initialization function
	bool Init(Level lvl, const char* logDirectory, const char* logFileNamePrefix, RotationKind rotation);

	// Destructor
	~Logger();

	// Logging methods
	void log(Level msgLevel, const char* message);

	template<typename... Args>
	void logf(Level msgLevel, const wchar_t* format, Args... args);

	template<typename... Args>
	void logf_utf8(Level msgLevel, const char* format, Args... args);

	template<typename... Args>
	void debug(const wchar_t* format, Args... args);

	template<typename... Args>
	void debug_utf8(const char* format, Args... args);

	template<typename... Args>
	void info(const wchar_t* format, Args... args);

	template<typename... Args>
	void info_utf8(const char* format, Args... args);

	template<typename... Args>
	void warn(const wchar_t* format, Args... args);

	template<typename... Args>
	void warn_utf8(const char* format, Args... args);

	template<typename... Args>
	void error(const wchar_t* format, Args... args);

	template<typename... Args>
	void error_utf8(const char* format, Args... args);
};

// logger.cpp
#include "logger.h"

// Static member initialization
Logger& Logger::GetInstance() {
	static Logger instance;
	return instance;
}

Logger::Logger()
	: buffer(BUFFER_SIZE), hFile(INVALID_HANDLE_VALUE), logThread(NULL) {
	// Initialize variables as needed
	level = LOG_DEBUG; // Default level
	rotationKind = ROTATE_NEVER;
	memset(logDir, 0, sizeof(logDir));
	memset(logFileName, 0, sizeof(logFileName));
	GetLocalTime(&lastRotationTime);
}

bool Logger::Init(Level lvl, const char* logDirectory, const char* logFileNamePrefix, RotationKind rotation) {
	level = lvl;
	rotationKind = rotation;

	strncpy_s(logDir, _countof(logDir), logDirectory, _TRUNCATE);
	strncpy_s(logFileName, _countof(logFileName), logFileNamePrefix, _TRUNCATE);

	GetLocalTime(&lastRotationTime);
	hFile = INVALID_HANDLE_VALUE;
	openLogFile();

	// Start background log writing thread
	logThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
	if (logThread == NULL) {
		fprintf(stderr, "Failed to create log thread.\n");
		return false;
	}
	return true;
}

Logger::~Logger() {
	running = false;
	if (logThread != NULL) {
		WaitForSingleObject(logThread, INFINITE);
		CloseHandle(logThread);
		logThread = NULL;
	}
	if (hFile != INVALID_HANDLE_VALUE) {
		FlushFileBuffers(hFile);
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
}

DWORD WINAPI Logger::ThreadFunc(void* data) {
	Logger* logger = static_cast<Logger*>(data);
	logger->processLogQueue();
	return 0;
}

void Logger::processLogQueue() {
	while (running) {
		auto currentTail = tail.load(std::memory_order_acquire);
		while (currentTail != head.load(std::memory_order_acquire)) {
			if (buffer[currentTail].ready.load(std::memory_order_acquire)) {
				checkRotation();
				std::lock_guard<std::mutex> lock(fileMutex);
				if (hFile != INVALID_HANDLE_VALUE) {
					DWORD written;
					WriteFile(hFile, buffer[currentTail].message.c_str(), static_cast<DWORD>(buffer[currentTail].message.size()), &written, NULL);
				}
				buffer[currentTail].ready.store(false, std::memory_order_release);
				currentTail = (currentTail + 1) % BUFFER_SIZE;
				tail.store(currentTail, std::memory_order_release);
			}
		}
		// Sleep briefly to prevent CPU hogging
		Sleep(10);
	}
	// Flush and close the file when exiting
	std::lock_guard<std::mutex> lock(fileMutex);
	if (hFile != INVALID_HANDLE_VALUE) {
		FlushFileBuffers(hFile);
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
}

void Logger::checkRotation() {
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
			currentTime.wYear != lastRotationTime.wYear) {
			shouldRotate = true;
		}
		break;
	case ROTATE_HOURLY:
		if (currentTime.wHour != lastRotationTime.wHour ||
			currentTime.wDay != lastRotationTime.wDay ||
			currentTime.wMonth != lastRotationTime.wMonth ||
			currentTime.wYear != lastRotationTime.wYear) {
			shouldRotate = true;
		}
		break;
	case ROTATE_DAILY:
		if (currentTime.wDay != lastRotationTime.wDay ||
			currentTime.wMonth != lastRotationTime.wMonth ||
			currentTime.wYear != lastRotationTime.wYear) {
			shouldRotate = true;
		}
		break;
	default:
		break;
	}

	if (shouldRotate) {
		std::lock_guard<std::mutex> lock(fileMutex);
		// Close current file
		if (hFile != INVALID_HANDLE_VALUE) {
			FlushFileBuffers(hFile);
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
		}
		// Update last rotation time
		lastRotationTime = currentTime;
		// Open new log file
		openLogFile();
	}
}

void Logger::openLogFile() {
	// Generate log file name
	char fullPath[MAX_PATH * 2];
	generateLogFileName(fullPath, rotationKind, lastRotationTime);

	// Create directory if it doesn't exist
	CreateDirectoryA(logDir, NULL);

	// Open the file
	hFile = CreateFileA(fullPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		// Handle error
		fprintf(stderr, "Failed to open log file: %s\n", fullPath);
	}
	else {
		SetFilePointer(hFile, 0, NULL, FILE_END);
	}
}

void Logger::generateLogFileName(char* fullPath, RotationKind rotationKind, const SYSTEMTIME& time) {
	char timestamp[64];
	switch (rotationKind) {
	case ROTATE_MINUTELY:
		sprintf_s(timestamp, "%04d%02d%02d_%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
		break;
	case ROTATE_HOURLY:
		sprintf_s(timestamp, "%04d%02d%02d_%02d", time.wYear, time.wMonth, time.wDay, time.wHour);
		break;
	case ROTATE_DAILY:
		sprintf_s(timestamp, "%04d%02d%02d", time.wYear, time.wMonth, time.wDay);
		break;
	case ROTATE_NEVER:
		strcpy_s(timestamp, "");
		break;
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

const char* Logger::levelToString(Level level) const {
	switch (level) {
	case LOG_DEBUG: return "DEBUG";
	case LOG_INFO:  return "INFO";
	case LOG_WARN:  return "WARN";
	case LOG_ERROR: return "ERROR";
	default:        return "UNKNOWN";
	}
}

std::string Logger::ConvertToUTF8(const wchar_t* message) {
	int bufferLength = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
	if (bufferLength == 0) {
		return "";
	}

	std::string utf8Message(bufferLength, 0);
	WideCharToMultiByte(CP_UTF8, 0, message, -1, &utf8Message[0], bufferLength, nullptr, nullptr);
	utf8Message.resize(bufferLength - 1); // Remove null terminator
	return utf8Message;
}

std::string Logger::currentTime() const {
	char buffer[32] = { 0 };
	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf_s(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	return buffer;
}

void Logger::log(Level msgLevel, const char* message) {
	if (msgLevel < level) {
		return;
	}

	std::string utf8Message(message);

	auto nextHead = (head.load(std::memory_order_relaxed) + 1) % BUFFER_SIZE;
	while (nextHead == tail.load(std::memory_order_acquire)) {
		// Buffer is full, wait or handle overflow
		Sleep(1);
	}

	std::string formattedMessage = currentTime() + " [" + levelToString(msgLevel) + "] " + utf8Message + "\n";
	buffer[head].message = formattedMessage;
	buffer[head].ready.store(true, std::memory_order_release);
	head.store(nextHead, std::memory_order_release);
}

template<typename... Args>
void Logger::logf(Level msgLevel, const wchar_t* format, Args... args) {
	if (msgLevel < level) {
		return;
	}

	int requiredSize = _scwprintf(format, args...) + 1;
	std::unique_ptr<wchar_t[]> formattedMessage(new wchar_t[requiredSize]);
	swprintf_s(formattedMessage.get(), requiredSize, format, args...);
	log(msgLevel, ConvertToUTF8(formattedMessage.get()).c_str());
}

template<typename... Args>
void Logger::logf_utf8(Level msgLevel, const char* format, Args... args) {
	if (msgLevel < level) {
		return;
	}

	int requiredSize = _scprintf(format, args...) + 1;
	std::unique_ptr<char[]> formattedMessage(new char[requiredSize]);
	sprintf_s(formattedMessage.get(), requiredSize, format, args...);
	log(msgLevel, formattedMessage.get());
}

template<typename... Args>
void Logger::debug(const wchar_t* format, Args... args) {
	logf(LOG_DEBUG, format, args...);
}

template<typename... Args>
void Logger::debug_utf8(const char* format, Args... args) {
	logf_utf8(LOG_DEBUG, format, args...);
}

template<typename... Args>
void Logger::info(const wchar_t* format, Args... args) {
	logf(LOG_INFO, format, args...);
}

template<typename... Args>
void Logger::info_utf8(const char* format, Args... args) {
	logf_utf8(LOG_INFO, format, args...);
}

template<typename... Args>
void Logger::warn(const wchar_t* format, Args... args) {
	logf(LOG_WARN, format, args...);
}

template<typename... Args>
void Logger::warn_utf8(const char* format, Args... args) {
	logf_utf8(LOG_WARN, format, args...);
}

template<typename... Args>
void Logger::error(const wchar_t* format, Args... args) {
	logf(LOG_ERROR, format, args...);
}

template<typename... Args>
void Logger::error_utf8(const char* format, Args... args) {
	logf_utf8(LOG_ERROR, format, args...);
}
