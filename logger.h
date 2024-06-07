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
#pragma warning(push)
#pragma warning(disable : 4267)
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
	TCHAR fileName[MAX_PATH];
	std::atomic<size_t> head{ 0 }, tail{ 0 };
	std::vector<LogEntry> buffer;
	HANDLE logThread;
	std::atomic<bool> running{ true };

	static DWORD WINAPI ThreadFunc(void* data) {
		Logger* logger = static_cast<Logger*>(data);
		logger->processLogQueue();
		return 0;
	}

	const TCHAR* levelToString(Level level) const {
		switch (level) {
		case LOG_DEBUG: return _T("DEBUG");
		case LOG_INFO:  return _T("INFO");
		case LOG_WARN:  return _T("WARN");
		case LOG_ERROR: return _T("ERROR");
		default:        return _T("UNKNOWN");
		}
	}

	TCHAR* currentTime() const {
		static TCHAR buffer[20];
		SYSTEMTIME st;
		GetLocalTime(&st);
		_stprintf_s(buffer, _countof(buffer), _T("%04d-%02d-%02d %02d:%02d:%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		return buffer;
	}

	void processLogQueue() {
		HANDLE hFile = CreateFile(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		SetFilePointer(hFile, 0, NULL, FILE_END);

		while (running) {
			auto currentTail = tail.load(std::memory_order_acquire);
			while (currentTail != head.load(std::memory_order_acquire)) {
				if (buffer[currentTail].ready.load(std::memory_order_acquire)) {
					DWORD written;
					// Write UTF-8 text to file
					WriteFile(hFile, buffer[currentTail].message.c_str(), buffer[currentTail].message.size(), &written, NULL);
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
	std::string ConvertToUTF8(const TCHAR* message) {
#ifdef UNICODE
		int bufferLength = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
		if (bufferLength == 0) {
			// throw std::runtime_error("Failed to get buffer length for UTF-8 conversion.");
			return "";
		}

		std::string utf8Message(bufferLength, 0);
		WideCharToMultiByte(CP_UTF8, 0, message, -1, &utf8Message[0], bufferLength, nullptr, nullptr);
		utf8Message.resize(bufferLength - 1);  // Remove the null terminator added by WideCharToMultiByte
		return utf8Message;
#else
		return std::string(message);
#endif
	}

public:
	Logger(Level lvl, const TCHAR* fName) : level(lvl), buffer(BUFFER_SIZE) {
		_tcsncpy_s(fileName, fName, MAX_PATH);
		logThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
	}

	~Logger() {
		running = false;
		WaitForSingleObject(logThread, INFINITE);
		CloseHandle(logThread);
	}

	void log(Level msgLevel, const TCHAR* message) {
		// std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		// std::string utf8Message = converter.to_bytes(message);  // Convert from TCHAR (UTF-16) to UTF-8
		std::string utf8Message = ConvertToUTF8(message);

		auto nextHead = (head.load(std::memory_order_relaxed) + 1) % BUFFER_SIZE;
		while (nextHead == tail.load(std::memory_order_acquire));

		// std::string formattedMessage = converter.to_bytes(currentTime()) + " [" + converter.to_bytes(levelToString(msgLevel)) + "] " + utf8Message + "\n";
		std::string formattedMessage = ConvertToUTF8(currentTime()) + " [" + ConvertToUTF8(levelToString(msgLevel)) + "] " + utf8Message + "\n";
		buffer[head].message = formattedMessage;
		buffer[head].ready.store(true, std::memory_order_release);
		head.store(nextHead, std::memory_order_release);
	}

	template<typename... Args>
	void logf(Level msgLevel, const TCHAR* format, Args... args) {
		// Assume formatting in TCHAR, convert to UTF-8 post formatting
		int requiredSize = _sctprintf(format, args...) + 1; // Calculate required size for the buffer
		std::unique_ptr<TCHAR[]> formattedMessage(new TCHAR[requiredSize]);
		_stprintf_s(formattedMessage.get(), requiredSize, format, args...);
		log(msgLevel, formattedMessage.get());
	}

	template<typename... Args>
	void debug(const TCHAR* format, Args... args) {
		logf(LOG_DEBUG, format, args...);
	}

	template<typename... Args>
	void info(const TCHAR* format, Args... args) {
		logf(LOG_INFO, format, args...);
	}

	template<typename... Args>
	void warn(const TCHAR* format, Args... args) {
		logf(LOG_WARN, format, args...);
	}

	template<typename... Args>
	void error(const TCHAR* format, Args... args) {
		logf(LOG_ERROR, format, args...);
	}
};
#pragma warning(pop)

//#include "Logger.h"
//
//int main() {
//	// 创建 Logger 对象，指定日志文件名和日志级别
//	Logger myLogger(LOG_INFO, _T("example_log.txt"));
//
//	// 使用不同的日志级别记录消息
//	myILogger.log(LOG_INFO, _T("This is an info message."));
//	myLogger.log(LOG_WARN, _T("This is a warning message."));
//	myLogger.log(LOG_ERROR, _T("This is an error message."));
//
//	// 使用格式化日志功能
//	myLogger.logf(LOG_DEBUG, _T("Logging %d + %d = %d"), 1, 2, 3);
//	myLogger.logf(LOG_INFO, _T("Current user is %s"), _T("John Doe"));
//
//	return 0;
//}

