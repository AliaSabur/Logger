// logger.h
#pragma once

#include <windows.h>
#include <atomic>
#include <vector>
#include <tchar.h>
#include <memory>
#include <string>
#include <locale>
#include <codecvt>

enum Level {
	LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR
};

class Logger {
private:
	struct LogEntry {
		std::atomic<bool> ready{ false };
		std::string message;  // Use std::string to store UTF-8 encoded strings
	};

	static constexpr size_t BUFFER_SIZE = 1024;
	Level level;
	char fileName[MAX_PATH];
	std::atomic<size_t> head{ 0 }, tail{ 0 };
	std::vector<LogEntry> buffer;
	HANDLE logThread;
	std::atomic<bool> running{ true };

	static DWORD WINAPI ThreadFunc(void* data) {
		Logger* logger = static_cast<Logger*>(data);
		logger->processLogQueue();
		return 0;
	}

	const char* levelToString(Level level) const {
		switch (level) {
		case LOG_DEBUG: return "DEBUG";
		case LOG_INFO:  return "INFO";
		case LOG_WARN:  return "WARN";
		case LOG_ERROR: return "ERROR";
		default:        return "UNKNOWN";
		}
	}

	char* currentTime() const {
		static char buffer[25] = { 0 };
		SYSTEMTIME st;
		GetLocalTime(&st);
		// _stprintf_s(buffer, _countof(buffer), _T("%04d-%02d-%02d %02d:%02d:%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		sprintf_s(buffer, _countof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		return buffer;
	}

	void processLogQueue() {
		HANDLE hFile = CreateFileA(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		SetFilePointer(hFile, 0, NULL, FILE_END);

		while (running) {
			auto currentTail = tail.load(std::memory_order_acquire);
			while (currentTail != head.load(std::memory_order_acquire)) {
				if (buffer[currentTail].ready.load(std::memory_order_acquire)) {
					DWORD written;
					// Write UTF-8 text to file
					WriteFile(hFile, buffer[currentTail].message.c_str(), static_cast<DWORD>(buffer[currentTail].message.size()), &written, NULL);
					buffer[currentTail].ready.store(false, std::memory_order_release);
					currentTail = (currentTail + 1) % BUFFER_SIZE;
					tail.store(currentTail, std::memory_order_release);
				}
			}
			FlushFileBuffers(hFile);
		}
		CloseHandle(hFile);
	}

	// Helper function for conversion
	std::string ConvertToUTF8(const wchar_t* message) {
		int bufferLength = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
		if (bufferLength == 0) {
			// throw std::runtime_error("Failed to get buffer length for UTF-8 conversion.");
			return "";
		}

		std::string utf8Message(bufferLength, 0);
		WideCharToMultiByte(CP_UTF8, 0, message, -1, &utf8Message[0], bufferLength, nullptr, nullptr);
		utf8Message.resize(bufferLength - 1);  // Remove the null terminator added by WideCharToMultiByte
		return utf8Message;
	}

public:
	Logger(Level lvl, const char* fName) : level(lvl), buffer(BUFFER_SIZE) {
		// _tcsncpy_s(fileName, fName, MAX_PATH);
		strncpy_s(fileName, _countof(fileName), fName, _TRUNCATE);
		logThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
	}

	~Logger() {
		running = false;
		WaitForSingleObject(logThread, INFINITE);
		CloseHandle(logThread);
	}

	void log(Level msgLevel, const char* message) {
		
		std::string utf8Message(message);

		auto nextHead = (head.load(std::memory_order_relaxed) + 1) % BUFFER_SIZE;
		while (nextHead == tail.load(std::memory_order_acquire));

		std::string formattedMessage = std::string(currentTime()) + " [" + std::string(levelToString(msgLevel)) + "] " + utf8Message + "\n";
		buffer[head].message = formattedMessage;
		buffer[head].ready.store(true, std::memory_order_release);
		head.store(nextHead, std::memory_order_release);
	}

	template<typename... Args>
	void logf(Level msgLevel, const wchar_t* format, Args... args) {
		// Assume formatting in TCHAR, convert to UTF-8 post formatting
		// int requiredSize = _sctprintf(format, args...) + 1; // Calculate required size for the buffer
		int requiredSize = _scwprintf(format, args...) + 1;
		std::unique_ptr<wchar_t[]> formattedMessage(new wchar_t[requiredSize]);
		// _stprintf_s(formattedMessage.get(), requiredSize, format, args...);
		swprintf_s(formattedMessage.get(), requiredSize, format, args...);
		// log(msgLevel, formattedMessage.get());
		log(msgLevel, ConvertToUTF8(formattedMessage.get()).c_str());
	}

	template<typename... Args>
	void logf_utf8(Level msgLevel, const char* format, Args... args) {
		// Assume formatting in TCHAR, convert to UTF-8 post formatting
		int requiredSize = _scprintf(format, args...) + 1; // Calculate required size for the buffer
		std::unique_ptr<char[]> formattedMessage(new char[requiredSize]);
		// _stprintf_s(formattedMessage.get(), requiredSize, format, args...);
		sprintf_s(formattedMessage.get(), requiredSize, format, args...);
		log(msgLevel, formattedMessage.get());
	}

	template<typename... Args>
	void debug(const wchar_t* format, Args... args) {
		logf(LOG_DEBUG, format, args...);
	}

	template<typename... Args>
	void debug_utf8(const char* format, Args... args) {
		logf_utf8(LOG_DEBUG, format, args...);
	}

	template<typename... Args>
	void info(const wchar_t* format, Args... args) {
		logf(LOG_INFO, format, args...);
	}

	template<typename... Args>
	void info_utf8(const char* format, Args... args) {
		logf_utf8(LOG_INFO, format, args...);
	}

	template<typename... Args>
	void warn(const wchar_t* format, Args... args) {
		logf(LOG_WARN, format, args...);
	}

	template<typename... Args>
	void warn_utf8(const char* format, Args... args) {
		logf_utf8(LOG_WARN, format, args...);
	}

	template<typename... Args>
	void error(const wchar_t* format, Args... args) {
		logf(LOG_ERROR, format, args...);
	}

	template<typename... Args>
	void error_utf8(const char* format, Args... args) {
		logf_utf8(LOG_ERROR, format, args...);
	}
};

//#include "Logger.h"
//
//int main() {
//	// 创建 Logger 对象，指定日志文件名和日志级别
//	Logger myLogger(LOG_INFO, _T("example_log.log"));
//
//	// 使用不同的日志级别记录消息
//	myLogger.log(LOG_INFO, _T("This is an info message."));
//	myLogger.log(LOG_WARN, _T("This is a warning message."));
//	myLogger.log(LOG_ERROR, _T("This is an error message."));
//
//	// 使用格式化日志功能
//	myLogger.logf(LOG_DEBUG, _T("Logging %d + %d = %d"), 1, 2, 3);
//	myLogger.logf(LOG_INFO, _T("Current user is %s"), _T("John Doe"));
//
//	return 0;
//}
