#ifndef SERVER_STATS_H
#define SERVER_STATS_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

// 服务端运行指标
struct ServerStats
{
    std::chrono::steady_clock::time_point start_time{
        std::chrono::steady_clock::now()};

    std::atomic<uint64_t> total_connections{0}; // 累计 accept 成功数
    std::atomic<uint64_t> total_commands{0};    // 累计处理命令数
    std::atomic<int64_t> connected_clients{0};  // 当前连接数

    uint64_t uptimeSeconds() const
    {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now - start_time)
                .count());
    }
};

// 运行时配置（第一版就是一张 key/value 表）
struct ServerConfig
{
    std::unordered_map<std::string, std::string> values;
};

#endif