#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <string>
#include <vector>

class RedisCommandHandler
{
public:
    RedisCommandHandler();

    // 入参改成已经解析好的 tokens
    std::string processCommand(const std::vector<std::string> &tokens);
};

#endif