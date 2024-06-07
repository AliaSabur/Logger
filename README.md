# Logger
一个高性能，无锁的 VC++ 的日志类

# 使用示例

```cpp
#include "Logger.h"

int main() {
	// 创建 Logger 对象，指定日志文件名和日志级别
	Logger myLogger(LOG_INFO, _T("example_log.log"));

	// 使用不同的日志级别记录消息
	myLogger.log(LOG_INFO, _T("This is an info message."));
	myLogger.log(LOG_WARN, _T("This is a warning message."));
	myLogger.log(LOG_ERROR, _T("This is an error message."));

	// 使用格式化日志功能
	myLogger.logf(LOG_DEBUG, _T("Logging %d + %d = %d"), 1, 2, 3);
	myLogger.logf(LOG_INFO, _T("Current user is %s"), _T("John Doe"));

	return 0;
}
```
