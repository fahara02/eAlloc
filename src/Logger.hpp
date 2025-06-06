#pragma once
#include <array>
#include <bitset>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdarg.h>
#include <string>
#include <unordered_set>
#include <cstdint>

namespace LOG
{
static constexpr uint16_t LOG_BUFFER_SIZE = 256;
enum class Level {
    DEBUG = 0, // Lowest priority (most verbose)
    TEST = 1,
    INFO = 2,
    SUCCESS = 3,
    WARNING = 4,
    ERROR = 5 // Highest priority (least verbose)
};

class Logger
{
   public:
    using LogCallback = std::function<void(const char* tag, Level level, const char* message)>;

    static Logger& getInstance()
    {
        static Logger instance;
        return instance;
    }
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    void vlog(const char* tag, Level level, const char* format, va_list args)
    {
        if(!enabled_ || level < minimumLevel_ || !isLevelEnabled(level) || blockedTags_.count(tag))
        {
            return;
        }
        logInternal(tag, level, format, args);
    }
    void vlog(Level level, const char* format, va_list args)
    {
        if(!enabled_ || level < minimumLevel_ || !isLevelEnabled(level))
        {
            return;
        }
        logInternal(nullptr, level, format, args);
    }
    void enableLevel(Level level) { enabledLevels_.set(static_cast<size_t>(level)); }
    void disableLevel(Level level) { enabledLevels_.reset(static_cast<size_t>(level)); }
    bool isLevelEnabled(Level level) const
    {
        return enabledLevels_.test(static_cast<size_t>(level));
    }
    void blockTag(const char* tag) { blockedTags_.insert(tag); }
    void unblockTag(const char* tag) { blockedTags_.erase(tag); }
    bool isTagBlocked(const char* tag) const { return blockedTags_.count(tag); }
    void enableTimestamps(bool enable = true) { timestampEnabled_ = enable; }
    void disableTimestamps() { timestampEnabled_ = false; }
    void setLogCallback(LogCallback callback) { logCallback_ = callback; }
    void setLevelColor(Level level, const char* color)
    {
        levelColors_[static_cast<size_t>(level)] = color;
    }
    void setInfoAlternateColor(const char* color) { infoAlternateColor_ = color; }
    void setMinimumLevel(Level level) { minimumLevel_ = level; }

   private:
    Logger() :
        enabled_(true),
        infoColorToggle_(false),
        infoAlternateColor_("\033[1;34m"),
        minimumLevel_(Level::DEBUG),
        timestampEnabled_(false),
        levelColors_{
            "\033[1;37m", // DEBUG: bright white
            "\033[1;35m", // TEST: bright magenta
            "\033[1;36m", // INFO: bright cyan
            "\033[1;32m", // SUCCESS: bright green
            "\033[1;33m", // WARNING: bright yellow
            "\033[1;31m"  // ERROR: bright red
        }
    {
        enabledLevels_.set(); // Enable all levels by default
    }
    bool enabled_;
    bool infoColorToggle_;
    const char* infoAlternateColor_;
    bool timestampEnabled_;
    Level minimumLevel_;
    std::bitset<6> enabledLevels_;
    std::array<const char*, 6> levelColors_;
    std::unordered_set<std::string> blockedTags_;
    LogCallback logCallback_;
    void logInternal(const char* tag, Level level, const char* format, va_list args)
    {
        char messageBuffer[LOG_BUFFER_SIZE];
        processFormat(messageBuffer, sizeof(messageBuffer), format, args);
        const char* colorCode;
        if(level == Level::INFO)
        {
            colorCode =
                infoColorToggle_ ? infoAlternateColor_ : levelColors_[static_cast<size_t>(level)];
            infoColorToggle_ = !infoColorToggle_;
        }
        else
        {
            colorCode = levelColors_[static_cast<size_t>(level)];
        }
        const char* levelStr = logLevelToString(level);
        if(tag != nullptr)
        {
            printf("%s[%s][%s] %s\033[0m\n", colorCode, levelStr, tag, messageBuffer);
            if(logCallback_)
            {
                logCallback_(tag, level, messageBuffer);
            }
        }
        else
        {
            printf("%s[%s] %s\033[0m\n", colorCode, levelStr, messageBuffer);
        }
    }
    void processFormat(char* buffer, size_t bufferSize, const char* format, va_list args)
    {
        if(strstr(format, "%b") || strstr(format, "%B"))
        {
            handleBinaryFormat(buffer, bufferSize, format, args);
        }
        else
        {
            vsnprintf(buffer, bufferSize, format, args);
        }
    }
    void formatBinary(char* buffer, size_t bufferSize, uint32_t value, int uppercase)
    {
        if(bufferSize < 2)
        {
            buffer[0] = '\0';
            return;
        }
        buffer[0] = '0';
        buffer[1] = uppercase ? 'B' : 'b';
        size_t pos = 2;
        uint32_t mask = 0x80000000;
        int started = 0;
        while(mask)
        {
            if(value & mask)
            {
                started = 1;
                buffer[pos++] = '1';
            }
            else if(started)
            {
                buffer[pos++] = '0';
            }
            if(pos >= bufferSize - 1)
            {
                break;
            }
            mask >>= 1;
        }
        if(!started && pos == 2)
        {
            buffer[pos++] = '0';
        }
        buffer[pos] = '\0';
    }
    void buildModifiedFormat(char* modifiedFormat, size_t formatSize, const char* format,
                             va_list args)
    {
        size_t pos = 0;
        const char* current = format;
        while(*current && pos < formatSize - 1)
        {
            if(strncmp(current, "%b", 2) == 0 || strncmp(current, "%B", 2) == 0)
            {
                bool isUpperCase = (current[1] == 'B');
                uint32_t value = va_arg(args, uint32_t);
                char binaryBuffer[40];
                formatBinary(binaryBuffer, sizeof(binaryBuffer), value, isUpperCase);
                size_t binaryLen = strlen(binaryBuffer);
                if(pos + binaryLen >= formatSize - 1)
                {
                    binaryLen = formatSize - pos - 1;
                    binaryBuffer[binaryLen] = '\0';
                }
                strncpy(modifiedFormat + pos, binaryBuffer, binaryLen);
                pos += binaryLen;
                current += 2;
            }
            else
            {
                modifiedFormat[pos++] = *current++;
            }
        }
        modifiedFormat[pos] = '\0';
    }
    void handleBinaryFormat(char* buffer, size_t bufferSize, const char* format, va_list args)
    {
        char modifiedFormat[LOG_BUFFER_SIZE];
        va_list argsCopy;
        va_copy(argsCopy, args);
        buildModifiedFormat(modifiedFormat, sizeof(modifiedFormat), format, argsCopy);
        va_end(argsCopy);
        vsnprintf(buffer, bufferSize, modifiedFormat, args);
    }
    const char* logLevelToString(Level level)
    {
        switch(level)
        {
            case Level::DEBUG:
                return "DEBUG";
            case Level::TEST:
                return "TEST";
            case Level::INFO:
                return "INFO";
            case Level::SUCCESS:
                return "SUCCESS";
            case Level::WARNING:
                return "WARNING";
            case Level::ERROR:
                return "ERROR";
            default:
                return "UNKNOWN";
        }
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

// Wrapper functions
static inline void DEBUG(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::DEBUG, format, args);
    va_end(args);
}
static inline void DEBUG(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::DEBUG, format, args);
    va_end(args);
}
static inline void LOG_TEST(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::TEST, format, args);
    va_end(args);
}
static inline void LOG_TEST(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::TEST, format, args);
    va_end(args);
}
static inline void INFO(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::INFO, format, args);
    va_end(args);
}
static inline void INFO(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::INFO, format, args);
    va_end(args);
}
static inline void SUCCESS(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::SUCCESS, format, args);
    va_end(args);
}
static inline void SUCCESS(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::SUCCESS, format, args);
    va_end(args);
}
static inline void WARNING(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::WARNING, format, args);
    va_end(args);
}
static inline void WARNING(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::WARNING, format, args);
    va_end(args);
}
static inline void ERROR(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(tag, Level::ERROR, format, args);
    va_end(args);
}
static inline void ERROR(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Logger::getInstance().vlog(Level::ERROR, format, args);
    va_end(args);
}
static inline void ENABLE() { Logger::getInstance().enable(); }
static inline void ENABLE(Level level)
{
    ENABLE();
    Logger::getInstance().enableLevel(level);
}
static inline void DISABLE() { Logger::getInstance().disable(); }
static inline void DISABLE(Level level) { Logger::getInstance().disableLevel(level); }
static inline void BLOCK_TAG(const char* tag) { Logger::getInstance().blockTag(tag); }
static inline void UNBLOCK_TAG(const char* tag) { Logger::getInstance().unblockTag(tag); }
static inline void ENABLE_TIMESTAMPS()
{
    ENABLE();
    Logger::getInstance().enableTimestamps(true);
}
static inline void DISABLE_TIMESTAMPS() { Logger::getInstance().disableTimestamps(); }

} // namespace LOG
