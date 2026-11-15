#include "logger.h"

bool Logger::Init(bool console)
{
    m_console = console;

    fs::path log;
    wchar_t buf[MAX_PATH];

    if (!GetModuleFileNameW(NULL, buf, MAX_PATH))
        return false;

    log = fs::path(buf).remove_filename() / "log.txt";

    m_file = CreateFileW(
        log.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (m_file == INVALID_HANDLE_VALUE)
        return false;

    // Cache timezone offset
    TIME_ZONE_INFORMATION tzInfo;
    DWORD tzResult = GetTimeZoneInformation(&tzInfo);

    LONG bias = tzInfo.Bias;

    if (tzResult == TIME_ZONE_ID_DAYLIGHT)
        bias += tzInfo.DaylightBias;
    else if (tzResult == TIME_ZONE_ID_STANDARD)
        bias += tzInfo.StandardBias;

    const int biasedOffsetMins = -static_cast<int>(bias);

    const int absoluteOffset = std::abs(biasedOffsetMins);
    const char offsetSign = biasedOffsetMins >= 0 ? '+' : '-';

    const int offsetHours = absoluteOffset / 60;
    const int offsetMins = absoluteOffset % 60;

    m_offsetStr =
        std::string(1, offsetSign) +
        (offsetHours < 10 ? "0" : "") +
        std::to_string(offsetHours) +
        ":" +
        (offsetMins < 10 ? "0" : "") +
        std::to_string(offsetMins);

    return true;
}

bool Logger::Remove()
{
    Log("Removing Logger...\n");

    if (!m_file)
        return true;

    bool result = CloseHandle(m_file);
    m_file = nullptr;

    return result;
}

void Logger::Log(const char* format, ...)
{
    SYSTEMTIME localTime;
    GetLocalTime(&localTime);

    char buf[MAX_PATH];

    int size = snprintf(
        buf,
        sizeof(buf),
        "[%02d:%02d:%02d%s] [TID: 0x%X] ",
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        m_offsetStr.c_str(),
        GetCurrentThreadId()
    );

    va_list args;
    va_start(args, format);

    va_list argsFile;
    va_copy(argsFile, args);

    int written = vsnprintf(
        buf + size,
        sizeof(buf) - size,
        format,
        argsFile
    );

    va_end(argsFile);

    if (written > 0)
        size += written;

    WriteFile(
        m_file,
        buf,
        size,
        NULL,
        NULL
    );

    if (m_console)
    {
        va_list argsConsole;
        va_copy(argsConsole, args);

        std::cout << "[LOG]";
        vprintf(format, argsConsole);

        va_end(argsConsole);
    }

    va_end(args);
}

void Logger::Log(const std::string& message)
{
    Logger::Log("%s", message.c_str());
}