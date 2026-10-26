#ifndef RESP_PARSER_H
#define RESP_PARSER_H

#include "Buffer.h"
#include <cstddef>
#include <string>
#include <vector>

enum class ParseStatus
{
    OK,       // 解析出一条完整命令
    NeedMore, // 数据不够，等下一次 recv
    Error     // 协议错误
};

struct ParseResult
{
    ParseStatus status = ParseStatus::NeedMore;
    std::vector<std::string> args;
    size_t consumed = 0;
};

class RespParser
{
public:
    // 从 Buffer 的未读区解析一条命令；不修改 Buffer
    static ParseResult parseNext(const Buffer &buf);

private:
    static ParseResult ParseMultibulk(const char *data, size_t len);
    static ParseResult ParseInline(const char *data, size_t len);
};
#endif