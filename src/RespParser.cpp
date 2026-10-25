#include "../include/RespParser.h"

#include <climits>
#include <string>
#include <utility> // std::move
#include <vector>

namespace
{
    constexpr size_t kMaxBulkLen = 16 * 1024 * 1024; // 单条 bulk 上限 16MB
    constexpr size_t kMaxLineLen = 64;               // 头部数字行上限
    constexpr int kMaxArgs = 1024 * 1024;            // 单条命令参数个数上限

    size_t findCRLF(const char *data, size_t start, size_t len)
    {
        for (size_t i = start; i + 1 < len; ++i)
        {
            if (data[i] == '\r' && data[i + 1] == '\n')
                return i;
        }
        return len;
    }

    bool parseUInt(const char *data, size_t len, int &out)
    {
        if (len == 0)
            return false;

        int value = 0;
        for (size_t i = 0; i < len; ++i)
        {
            char c = data[i];
            if (c < '0' || c > '9')
                return false;
            int d = c - '0';
            if (value > (INT_MAX - d) / 10)
                return false;
            value = value * 10 + d;
        }
        out = value;
        return true;
    }
} // namespace

ParseResult RespParser::parseNext(const Buffer &buf)
{
    if (buf.readAble() == 0)
        return {ParseStatus::NeedMore};

    const char *data = buf.Peek();
    size_t len = buf.readAble();

    if (data[0] != '*')
        return {ParseStatus::NeedMore}; // inline 命令下一步再做

    return ParseMultibulk(data, len);
}

ParseResult RespParser::ParseMultibulk(const char *data, size_t len)
{
    size_t pos = 1;
    size_t crlf = findCRLF(data, pos, len);
    if (crlf == len)
    {
        if (len > kMaxLineLen)
            return {ParseStatus::Error};
        return {ParseStatus::NeedMore};
    }

    int count = 0;
    if (!parseUInt(data + pos, crlf - pos, count) || count <= 0 || count > kMaxArgs)
        return {ParseStatus::Error};

    pos = crlf + 2;
    std::vector<std::string> args;

    for (int i = 0; i < count; ++i)
    {
        if (pos >= len)
            return {ParseStatus::NeedMore};

        if (data[pos] != '$')
            return {ParseStatus::Error};
        ++pos;

        crlf = findCRLF(data, pos, len);
        if (crlf == len)
        {
            if (len - pos > kMaxLineLen)
                return {ParseStatus::Error};
            return {ParseStatus::NeedMore};
        }

        int bulkLen = 0;
        if (!parseUInt(data + pos, crlf - pos, bulkLen) ||
            static_cast<size_t>(bulkLen) > kMaxBulkLen) // 这里别用 kMaxLineLen
            return {ParseStatus::Error};

        pos = crlf + 2;
        if (pos + static_cast<size_t>(bulkLen) + 2 > len)
            return {ParseStatus::NeedMore};

        if (data[pos + bulkLen] != '\r' || data[pos + bulkLen + 1] != '\n')
            return {ParseStatus::Error};

        args.emplace_back(data + pos, static_cast<size_t>(bulkLen));
        pos += static_cast<size_t>(bulkLen) + 2;
    }

    return {ParseStatus::OK, std::move(args), pos};
}