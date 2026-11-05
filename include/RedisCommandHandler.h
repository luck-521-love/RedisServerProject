#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <string>
#include <vector>

#include "ServerStats.h"

class RedisCommandHandler
{
public:
    explicit RedisCommandHandler(ServerStats *stats = nullptr,
                                 ServerConfig *config = nullptr);

    std::string processCommand(const std::vector<std::string> &tokens);

private:
    // CONFIG / INFO 需要访问统计和配置，所以做成成员函数
    std::string handleConfig(const std::vector<std::string> &tokens);
    std::string handleInfo(const std::vector<std::string> &tokens);

    ServerStats *stats_;
    ServerConfig *config_;
};

#endif