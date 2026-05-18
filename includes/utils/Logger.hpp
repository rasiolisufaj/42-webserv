#pragma once
#include <iostream>
#include <sstream>
#include <string>

class Logger
{
public:
    enum Level
    {
        DEBUG, // Detailed internal debugging
        INFO,  // Normal server activity
        WARNING, // Something suspicious
        ERROR // Failure/problem
    };
    /*
        * Logs a message with the specified log level.
        * The message is formatted as "[LEVEL] message".
        * Notes:
            - Messages below the current level are ignored.
            - Error goes to stderr, others to stdout.
    */
   static void  log(Level level, const std::string &message);

    // Set the minimum log level. Messages below this level will be ignored.
    static void setLogLevel(Level level);

private:
    static Level _currentLevel;

    /*
        * Get the current timestamp in the format "YYYY-MM-DD HH:MM:SS".
    */
    static std::string _getTimestamp();

    /*
        * Convert a log level to its string representation.
        * For example, Level::DEBUG -> "DEBUG".
    */
    static std::string _levelToString(Level level);

    /*
        * Returns an ANSI color escape for the given log level.
    */
    static std::string _getColor(Level level);
    /*
        * Utility class: prevent instantiation/copying of Logger.
    */

    Logger();
    Logger(const Logger &other);
    Logger &operator=(const Logger &rhs);
    ~Logger();
};

/*
    * Generic value-to-string helper used throughout the project.
*/
template <typename T>
std::string to_string(const T &value)
{   
    std::ostringstream oss;
    oss << value;
    return oss.str();
}
