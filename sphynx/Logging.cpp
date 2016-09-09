#include "Logging.h"

#ifndef ANDROID
#include <iostream>
#endif

namespace logging {


std::atomic<Level> MinLevel = Level::Debug;


const char* LevelToString(Level level)
{
    static_assert((int)Level::Count == 5, "Update this switch");
    switch (level)
    {
    case Level::Trace: return "Trace";
    case Level::Debug: return "Debug";
    case Level::Info: return "Info";
    case Level::Warning: return "Warning";
    case Level::Error: return "Error";
    default: break;
    }
    DEBUG_BREAK;
    return "Unknown";
}

char LevelToChar(Level level)
{
    static_assert((int)Level::Count == 5, "Update this switch");
    switch (level)
    {
    case Level::Trace: return 't';
    case Level::Debug: return 'd';
    case Level::Info: return 'I';
    case Level::Warning: return 'W';
    case Level::Error: return '!';
    default: break;
    }
    DEBUG_BREAK;
    return '?';
}


OutputWorker& OutputWorker::GetInstance()
{
    static OutputWorker worker;
    return worker;
}

extern "C" void AtExitWrapper()
{
    OutputWorker::GetInstance().Stop();
}

OutputWorker::OutputWorker()
{
    Terminated = true;

    Start();

    std::atexit(AtExitWrapper);
}

void OutputWorker::Start()
{
    Stop();

    QueuePublic.clear();
    QueuePrivate.clear();
    Overrun = 0;
    Terminated = false;
    Thread = std::make_unique<std::thread>(&OutputWorker::Loop, this);
}

void OutputWorker::Stop()
{
    if (Thread)
    {
        Terminated = true;
        Condition.notify_all();

        try
        {
            Thread->join();
        }
        catch (std::system_error& /*err*/)
        {
        }
    }
    Thread = nullptr;
}

void OutputWorker::Write(LogStringBuffer& buffer)
{
    std::string str = buffer.LogStream.str();

    {
        std::lock_guard<std::mutex> locker(QueueLock);
        if (static_cast<int>(QueuePublic.size()) >= WorkQueueLimit)
            Overrun++;
        else
            QueuePublic.emplace_back(buffer.LogLevel, buffer.ChannelName, str);
    }

    Condition.notify_all();
}

void OutputWorker::Loop()
{
    while (!Terminated)
    {
        int overrun;
        {
            std::unique_lock<std::mutex> locker(QueueLock);
            Condition.wait(locker);

            std::swap(QueuePublic, QueuePrivate);
            overrun = Overrun;
        }

        for (auto& log : QueuePrivate)
        {
            Log(log);
        }

        QueuePrivate.clear();
    }
}

void OutputWorker::Log(QueuedMessage& message)
{
    std::stringstream ss;
    ss << '{' << LevelToChar(message.LogLevel) << '-' << message.ChannelName << "} " << message.Message;

#ifdef ANDROID
    std::string fmtstr = ss.str();
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", fmtstr.c_str());
#else
    ss << std::endl;
    std::string fmtstr = ss.str();
    std::cout << fmtstr;
    #ifdef _WIN32
        ::OutputDebugStringA(fmtstr.c_str());
    #endif
#endif
}


Channel::Channel(const char* name)
{
    ChannelName = name;
}

std::string Channel::GetPrefix() const
{
    Locker locker(PrefixLock);
    return Prefix;
}

void Channel::SetPrefix(const std::string& prefix)
{
    Locker locker(PrefixLock);
    Prefix = prefix;
}


} // namespace logging
