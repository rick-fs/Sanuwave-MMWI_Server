#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <ios>

enum class LogLevel
{
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger
{
  public:
    static Logger &getInstance()
    {
        static Logger instance;
        return instance;
    }

    void setLogLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        minLevel_ = level;
    }

    void setLogFile(const std::string &filename,std::ios_base::openmode mode = std::ios::app)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logFile_.open(filename, mode);
        if (!logFile_.is_open())
        {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
    }

    void setLogFileWithNoTimestamp(const std::string &baseDir, const std::string &prefix = "log",std::ios_base::openmode mode = std::ios::app)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ostringstream filename;
        filename << baseDir;
        if (!baseDir.empty() && baseDir.back() != '/')
        {
            filename << '/';
        }
        filename << prefix << "_"<< ".log";

        logFile_.open(filename.str(), mode);
        if (!logFile_.is_open())
        {
            std::cerr << "Failed to open log file: " << filename.str() << std::endl;
        }
        else
        {
            currentLogFile_ = filename.str();
            std::cout << "Logging to: " << currentLogFile_ << std::endl;
        }
    }

    void setLogFileWithTimestamp(const std::string &baseDir, const std::string &prefix = "log")
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::ostringstream filename;
        filename << baseDir;
        if (!baseDir.empty() && baseDir.back() != '/')
        {
            filename << '/';
        }
        filename << prefix << "_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S")
                 << ".log";

        logFile_.open(filename.str(), std::ios::app);
        if (!logFile_.is_open())
        {
            std::cerr << "Failed to open log file: " << filename.str() << std::endl;
        }
        else
        {
            currentLogFile_ = filename.str();
            std::cout << "Logging to: " << currentLogFile_ << std::endl;
        }
    }

    std::string getCurrentLogFile() const
    {
        return currentLogFile_;
    }

    void enableConsoleOutput(bool enable)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        consoleOutput_ = enable;
    }

    class LogStream
    {
      public:
        LogStream(Logger &logger, LogLevel level, const char* file, int line)
            : logger_(logger), level_(level), file_(file), line_(line)
        {
            if (shouldLog())
            {
                stream_ << getTimestamp() << " " << getLevelString() << " "
                        << "[" << extractFilename(file_) << ":" << line_ << "] ";
            }
        }

        ~LogStream()
        {
            if (shouldLog())
            {
                logger_.write(stream_.str(), level_);
            }
        }

        template <typename T> LogStream &operator<<(const T &value)
        {
            if (shouldLog())
            {
                stream_ << value;
            }
            return *this;
        }

        // std::endl, std::flush, std::ws — manipulators on std::ostream.
        LogStream &operator<<(std::ostream &(*manip)(std::ostream &))
        {
            if (shouldLog())
            {
                stream_ << manip;
            }
            return *this;
        }

        // std::hex, std::dec, std::oct, std::showbase, std::noshowbase,
        // std::boolalpha, std::noboolalpha, std::uppercase, std::nouppercase,
        // std::left, std::right, std::internal — manipulators on
        // std::ios_base. Without this overload they bind to the templated
        // operator<<(const T&) above, which calls stream_ << manip on a
        // function pointer rather than invoking it as a manipulator —
        // silently breaking the resulting log line.
        LogStream &operator<<(std::ios_base &(*manip)(std::ios_base &))
        {
            if (shouldLog())
            {
                stream_ << manip;
            }
            return *this;
        }

        // std::ios manipulators (rare but possible: std::skipws, std::noskipws).
        LogStream &operator<<(std::ios &(*manip)(std::ios &))
        {
            if (shouldLog())
            {
                stream_ << manip;
            }
            return *this;
        }

      private:
        bool shouldLog() const
        {
            return level_ >= logger_.minLevel_;
        }

        std::string getTimestamp() const
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
                1000;

            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.'
                << std::setfill('0') << std::setw(3) << ms.count();
            return oss.str();
        }

        std::string getLevelString() const
        {
            switch (level_)
            {
            case LogLevel::TRACE:
                return "[TRACE]   ";
            case LogLevel::DEBUG:
                return "[DEBUG]   ";
            case LogLevel::INFO:
                return "[INFO]    ";
            case LogLevel::WARNING:
                return "[WARNING] ";
            case LogLevel::ERROR:
                return "[ERROR]   ";
            case LogLevel::CRITICAL:
                return "[CRITICAL]";
            default:
                return "[UNKNOWN]";
            }
        }

        std::string extractFilename(const char* path) const
        {
            std::string p(path);
            size_t pos = p.find_last_of("/\\");
            return (pos == std::string::npos) ? p : p.substr(pos + 1);
        }

        Logger &logger_;
        LogLevel level_;
        const char* file_;
        int line_;
        std::ostringstream stream_;
    };

    class FunctionTimer
    {
      public:
        FunctionTimer(Logger &logger, const char* function, const char* file, int line)
            : logger_(logger), function_(function), file_(file), line_(line),
              start_(std::chrono::high_resolution_clock::now())
        {
        }

        ~FunctionTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);
            
            std::ostringstream oss;
            oss << getTimestamp() << " [TIME]     "
                << "[" << extractFilename(file_) << ":" << line_ << "] "
                << function_ << " took " << duration.count() << " ms\n";
            
            logger_.write(oss.str(), LogLevel::INFO);
        }

      private:
        std::string getTimestamp() const
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
                1000;

            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.'
                << std::setfill('0') << std::setw(3) << ms.count();
            return oss.str();
        }

        std::string extractFilename(const char* path) const
        {
            std::string p(path);
            size_t pos = p.find_last_of("/\\");
            return (pos == std::string::npos) ? p : p.substr(pos + 1);
        }

        Logger &logger_;
        const char* function_;
        const char* file_;
        int line_;
        std::chrono::high_resolution_clock::time_point start_;
    };

    LogStream log(LogLevel level, const char* file, int line)
    {
        return {*this, level, file, line};
    }

    FunctionTimer timeFunction(const char* function, const char* file, int line)
    {
        return {*this, function, file, line};
    }

  private:
    Logger() : minLevel_(LogLevel::INFO), consoleOutput_(true), currentLogFile_("")
    {
    }
    ~Logger()
    {
        if (logFile_.is_open())
        {
            logFile_.close();
        }
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void write(const std::string &message, LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (consoleOutput_)
        {
            if (level >= LogLevel::ERROR)
            {
                std::cerr << message;
            }
            else
            {
                std::cout << message;
            }
        }

        if (logFile_.is_open())
        {
            logFile_ << message;
            logFile_.flush();
        }
    }

    LogLevel minLevel_;
    bool consoleOutput_;
    std::ofstream logFile_;
    std::mutex mutex_;
    std::string currentLogFile_;
};

// Convenience macros
#define LOG(level) Logger::getInstance().log(LogLevel::level, __FILE__, __LINE__)
#define LOG_TRACE LOG(TRACE)
#define LOG_DEBUG LOG(DEBUG)
#define LOG_INFO LOG(INFO)
#define LOG_WARNING LOG(WARNING)
#define LOG_ERROR LOG(ERROR)
#define LOG_CRITICAL LOG(CRITICAL)

// Timer macro - place at the beginning of a function to time its execution
#define LOG_FUNCTION_TIME() \
    Logger::FunctionTimer _function_timer_##__LINE__(Logger::getInstance(), __FUNCTION__, __FILE__, __LINE__)

#endif // LOGGER_H
