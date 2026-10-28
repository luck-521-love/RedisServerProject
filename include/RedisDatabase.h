#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>

enum Status
{
    OK,
    WrongType,
    NotFound
};

class RedisDatabase
{
public:
    static RedisDatabase &getInstance();

    // Common Comands
    bool flushAll();

    long long ttl(const std::string &key);  // 秒：-2 不存在；-1 无 TTL；否则剩余秒数（四舍五入）
    long long pttl(const std::string &key); // 毫秒：-2 / -1 / 剩余毫秒
    bool persist(const std::string &key);   // 有 TTL 并移除 → true；否则 false
    // Key/Value Operations
    void set(const std::string &key, const std::string &value);
    Status get(const std::string &key, std::string &value);
    std::vector<std::string> keys();
    std::string type(const std::string &key);
    bool del(const std::string &key);
    bool expire(const std::string &key, int seconds);
    void purgeExpired();
    bool rename(const std::string &oldKey, const std::string &newKey);

    // List Opreations
    Status lget(const std::string &key, std::vector<std::string> &out);
    Status llen(const std::string &key, ssize_t &out);
    Status lpush(const std::string &key, const std::string &value);
    Status rpush(const std::string &key, const std::string &value);
    Status lpop(const std::string &key, std::string &value);
    Status rpop(const std::string &key, std::string &value);
    Status lrem(const std::string &key, int count, const std::string &value, int &removed);
    Status lindex(const std::string &key, int index, std::string &value);
    Status lset(const std::string &key, int index, const std::string &value);

    // Hash Operations
    bool hset(const std::string &key, const std::string &field, const std::string &value);
    bool hget(const std::string &key, const std::string &field, std::string &value);
    bool hexists(const std::string &key, const std::string &field);
    bool hdel(const std::string &key, const std::string &field);
    std::unordered_map<std::string, std::string> hgetall(const std::string &key);
    std::vector<std::string> hkeys(const std::string &key);
    std::vector<std::string> hvals(const std::string &key);
    ssize_t hlen(const std::string &key);
    bool hmset(const std::string &key, const std::vector<std::pair<std::string, std::string>> &fieldValues);

    // Persistance: Dump / load the database from a file.
    bool dump(const std::string &filename);
    bool load(const std::string &filename);

private:
    RedisDatabase() = default;
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase &) = delete;
    RedisDatabase &operator=(const RedisDatabase) = delete;

    enum class KeyType
    {
        None,
        String,
        List,
        Hash
    };
    KeyType keyTypeOf(const std::string &key) const;

    // 调用者必须已持有 db_mutex,RedisDatabase内部辅助函数
    bool removeIfExpired(const std::string &key);

    std::mutex db_mutex;
    std::unordered_map<std::string, std::string> kv_store;
    std::unordered_map<std::string, std::vector<std::string>> list_store;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_store;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry_map;
};

#endif