#include "utils/Logger.hpp"
#include <ctime>

/*
 * Default to INFO to keep the server responsive during stress tests.
 */
Logger::Level Logger::_currentLevel = Logger::INFO;

/*
 * Sets the minimum log level. Messages below this level will be ignored.
*/
void Logger::setLogLevel(Level level)
{
    _currentLevel = level;
}

/*
 * Formats the current local time as a timestamp for log lines.
 */
std::string Logger::_getTimestamp()
{
    std::time_t now = std::time(NULL);
    std::tm *tm_info = std::localtime(&now);
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

/*
 * Returns an ANSI color code for the given log level.
 */
std::string Logger::_getColor(Level level)
{
    switch (level)
    {
    case DEBUG:
        return "\033[36m"; // Cyan
    case INFO:
        return "\033[32m"; // Green
    case WARNING:
        return "\033[33m"; // Yellow
    case ERROR:
        return "\033[31m"; // Red
    default:
        return "\033[0m"; // Reset
    }
}

/*
 * Returns a short string label for the given log level.
*/
std::string Logger::_levelToString(Level level)
{
    switch (level)
    {
    case DEBUG:
        return "[DEBUG]";
    case INFO:
        return "[INFO]";
    case WARNING:
        return "[WARNING]";
    case ERROR:
        return "[ERROR]";
    default:
        return "[UNKNOWN]";
    }
}

/*
 * Prints a log line if the provided level passes the current filter.
 */
void Logger::log(Level level, const std::string &message)
{
    if (level < _currentLevel)
        return;
    
    std::ostream &out = (level == ERROR) ? std::cerr : std::cout;

    out << _getColor(level) << "[" << _getTimestamp() << "]" << " " << _levelToString(level) << " " << message << "\033[0m" << std::endl;
}

