#pragma once

#include "Tools.h"
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <list>

namespace logging {


enum class Level
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,

    Count // For static assert
};

const char* LevelToString(Level level);
char LevelToChar(Level level);

struct LogStringBuffer
{
    const char* ChannelName;
    Level LogLevel;
    std::stringstream LogStream;

    LogStringBuffer(const char* channel, Level level) :
        ChannelName(channel),
        LogLevel(level),
        LogStream()
    {
    }
};

template<typename T>
FORCE_INLINE void LogStringize(LogStringBuffer& buffer, const T& first)
{
    buffer.LogStream << first;
}

// Overrides for various types we want to handle specially:

template<>
FORCE_INLINE void LogStringize(LogStringBuffer& buffer, const bool& first)
{
    buffer.LogStream << (first ? "true" : "false");
}


extern std::atomic<Level> MinLevel;

FORCE_INLINE void SetMinLevel(Level level)
{
    MinLevel.store(level, std::memory_order_relaxed);
}
FORCE_INLINE Level GetMinLevel()
{
    return MinLevel.load(std::memory_order_relaxed);
}


class OutputWorker
{
    OutputWorker();

public:
    static OutputWorker& GetInstance();
    void Write(LogStringBuffer& buffer);
    void Start();
    void Stop();

private:
    struct QueuedMessage
    {
        Level LogLevel;
        const char* ChannelName;
        std::string Message;

        QueuedMessage(Level level, const char* channel, std::string& message)
            : LogLevel(level)
            , ChannelName(channel)
            , Message(message)
        {
        }
    };

    static const int WorkQueueLimit = 32;

    std::mutex QueueLock;
    std::condition_variable Condition;
    std::list<QueuedMessage> QueuePublic;
    std::list<QueuedMessage> QueuePrivate;
    int Overrun = 0;

    std::unique_ptr<std::thread> Thread;
    std::atomic_bool Terminated;

    void Loop();

    void Log(QueuedMessage& message);
};


class Channel
{
public:
    explicit Channel(const char* name);

    std::string GetPrefix() const;
    void SetPrefix(const std::string& prefix);

    template<typename... Args>
    FORCE_INLINE void Log(Level level, Args&&... args) const
    {
        if (level >= GetMinLevel())
            log(level, std::forward<Args>(args)...);
    }

    template<typename... Args>
    FORCE_INLINE void Error(Args&&... args) const
    {
        if (Level::Error >= GetMinLevel())
            log(Level::Error, std::forward<Args>(args)...);
    }

    template<typename... Args>
    FORCE_INLINE void Warning(Args&&... args) const
    {
        if (Level::Warning >= GetMinLevel())
            log(Level::Warning, std::forward<Args>(args)...);
    }

    template<typename... Args>
    FORCE_INLINE void Info(Args&&... args) const
    {
        if (Level::Info >= GetMinLevel())
            log(Level::Info, std::forward<Args>(args)...);
    }

    template<typename... Args>
    FORCE_INLINE void Debug(Args&&... args) const
    {
        if (Level::Debug >= GetMinLevel())
            log(Level::Debug, std::forward<Args>(args)...);
    }

    template<typename... Args>
    FORCE_INLINE void Trace(Args&&... args) const
    {
        if (Level::Trace >= GetMinLevel())
            log(Level::Trace, std::forward<Args>(args)...);
    }

private:
    const char* ChannelName;

    mutable Lock PrefixLock;
    std::string Prefix;

    template<typename T>
    FORCE_INLINE void writeLogBuffer(LogStringBuffer& buffer, T&& arg) const
    {
        LogStringize(buffer, arg);
    }

    template<typename T, typename... Args>
    FORCE_INLINE void writeLogBuffer(LogStringBuffer& buffer, T&& arg, Args&&... args) const
    {
        writeLogBuffer(buffer, arg);
        writeLogBuffer(buffer, args...);
    }

    template<typename... Args>
    FORCE_INLINE void log(Level level, Args&&... args) const
    {
        LogStringBuffer buffer(ChannelName, level);
        writeLogBuffer(buffer, Prefix, args...);
        OutputWorker::GetInstance().Write(buffer);
    }
};


} // namespace logging
