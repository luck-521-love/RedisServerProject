#include "../include/RedisDatabase.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <unordered_set>

RedisDatabase &RedisDatabase::getInstance()
{
    static RedisDatabase instance;
    return instance;
}

// 如果已过期则删除
bool RedisDatabase::removeIfExpired(const std::string &key)
{
    auto expireIt = expiry_map.find(key);
    if (expireIt == expiry_map.end())
        return false;
    if (std::chrono::steady_clock::now() < expireIt->second)
        return false;
    kv_store.erase(key);
    list_store.erase(key);
    hash_store.erase(key);
    expiry_map.erase(expireIt);
    return true;
}

bool RedisDatabase::flushAll()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    kv_store.clear();
    list_store.clear();
    hash_store.clear();
    expiry_map.clear();
    return true;
}

long long RedisDatabase::ttl(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex); //  必须加锁

    if (removeIfExpired(key)) // 已过期 = 不存在
        return -2;

    auto it = expiry_map.find(key); // 没有 TTL 记录
    if (it == expiry_map.end())
    {
        bool exists = kv_store.count(key) ||
                      list_store.count(key) ||
                      hash_store.count(key);
        return exists ? -1 : -2;
    }

    auto now = std::chrono::steady_clock::now(); // 算剩余
    auto remain = it->second - now;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remain).count();

    return (ms + 500) / 1000; // 四舍五入到秒
}

long long RedisDatabase::pttl(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return -2;

    auto it = expiry_map.find(key); // 没有 TTL 记录
    if (it == expiry_map.end())
    {
        bool exists = kv_store.count(key) ||
                      list_store.count(key) ||
                      hash_store.count(key);
        return exists ? -1 : -2;
    }

    auto now = std::chrono::steady_clock::now();
    auto remain = it->second - now;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remain).count();
    return ms;
}

bool RedisDatabase::persist(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    if (removeIfExpired(key))
        return false;

    auto it = expiry_map.find(key);

    if (it == expiry_map.end())
        return false;
    expiry_map.erase(it);
    return true;
}

RedisDatabase::KeyType RedisDatabase::keyTypeOf(const std::string &key) const
{
    if (kv_store.find(key) != kv_store.end())
        return KeyType::String;
    if (list_store.find(key) != list_store.end())
        return KeyType::List;
    if (hash_store.find(key) != hash_store.end())
        return KeyType::Hash;
    return KeyType::None;
}

// Key/Value Operations
void RedisDatabase::set(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    removeIfExpired(key); // 清理过期残留数据
    list_store.erase(key);
    hash_store.erase(key);
    expiry_map.erase(key); // 清除旧的TTL (不管有没有)
    kv_store[key] = value;
    return;
}

Status RedisDatabase::get(const std::string &key, std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    removeIfExpired(key);

    KeyType t = keyTypeOf(key);
    if (t == KeyType::List || t == KeyType::Hash)
        return Status::WrongType;
    if (t == KeyType::None)
        return Status::NotFound;

    value = kv_store.find(key)->second;
    return Status::OK;
}

std::vector<std::string> RedisDatabase::keys()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpired();

    std::unordered_set<std::string> seen;
    std::vector<std::string> result;
    for (const auto &pair : kv_store)

        if (seen.insert(pair.first).second)
            result.push_back(pair.first);

    for (const auto &pair : list_store)

        if (seen.insert(pair.first).second)
            result.push_back(pair.first);

    for (const auto &pair : hash_store)
        if (seen.insert(pair.first).second)
            result.push_back(pair.first);

    return result;
}

std::string RedisDatabase::type(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return "none";
    if (kv_store.find(key) != kv_store.end())
    {
        return "string";
    }

    if (list_store.find(key) != list_store.end())
    {
        return "list";
    }

    if (hash_store.find(key) != hash_store.end())
    {
        return "hash";
    }
    return "none";
}

bool RedisDatabase::del(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpired();
    bool erase = false;
    erase |= kv_store.erase(key) > 0;
    erase |= list_store.erase(key) > 0;
    erase |= hash_store.erase(key) > 0;
    expiry_map.erase(key);
    return erase;
}

bool RedisDatabase::expire(const std::string &key, int second)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    if ((kv_store.find(key) == kv_store.end()) && (list_store.find(key) == list_store.end()) && (hash_store.find(key) == hash_store.end()))
        return false;

    if (second <= 0)
    {
        kv_store.erase(key);
        list_store.erase(key);
        hash_store.erase(key);
        expiry_map.erase(key);
        return true;
    }
    expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::seconds(second);
    return true;
}

void RedisDatabase::purgeExpired()
{
    auto now = std::chrono::steady_clock::now();

    for (auto it = expiry_map.begin(); it != expiry_map.end();)
    {
        if (now > it->second)
        {
            kv_store.erase(it->first);
            list_store.erase(it->first);
            hash_store.erase(it->first);
            it = expiry_map.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool RedisDatabase::rename(const std::string &oldKey, const std::string &newKey)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    // 只惰性清理这两个 key，不做全表扫描
    removeIfExpired(oldKey);
    removeIfExpired(newKey);

    auto exists = [&](const std::string &k)
    {
        return kv_store.find(k) != kv_store.end() ||
               list_store.find(k) != list_store.end() ||
               hash_store.find(k) != hash_store.end();
    };

    // 同名：存在就成功且不改变任何数据
    if (oldKey == newKey)
        return exists(oldKey);

    if (!exists(oldKey))
        return false;

    // 记录源 key 的 TTL（如果有）
    bool hasTtl = false;
    std::chrono::steady_clock::time_point deadline;
    auto eit = expiry_map.find(oldKey);
    if (eit != expiry_map.end())
    {
        hasTtl = true;
        deadline = eit->second;
    }

    // 目标 key 的旧数据与旧 TTL 全部清掉
    kv_store.erase(newKey);
    list_store.erase(newKey);
    hash_store.erase(newKey);
    expiry_map.erase(newKey);

    // 搬 kv
    auto it = kv_store.find(oldKey);
    if (it != kv_store.end())
    {
        std::string tmp = std::move(it->second);
        kv_store.erase(it);
        kv_store.emplace(newKey, std::move(tmp));
    }

    // 搬 list
    auto lit = list_store.find(oldKey);
    if (lit != list_store.end())
    {
        std::vector<std::string> tmp = std::move(lit->second);
        list_store.erase(lit);
        list_store.emplace(newKey, std::move(tmp));
    }

    // 搬 hash
    auto hit = hash_store.find(oldKey);
    if (hit != hash_store.end())
    {
        std::unordered_map<std::string, std::string> tmp = std::move(hit->second);
        hash_store.erase(hit);
        hash_store.emplace(newKey, std::move(tmp));
    }

    // 迁移 TTL
    expiry_map.erase(oldKey);
    if (hasTtl)
        expiry_map[newKey] = deadline;

    return true;
}

// List Opreations
Status RedisDatabase::lget(const std::string &key, std::vector<std::string> &out)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    removeIfExpired(key);

    KeyType t = keyTypeOf(key);
    if (t != KeyType::None && t != KeyType::List)
        return Status::WrongType;
    if (t == KeyType::None)
        return Status::NotFound;
    out = list_store.find(key)->second;
    return Status::OK;
}

Status RedisDatabase::llen(const std::string &key, ssize_t &out)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    removeIfExpired(key);
    KeyType t = keyTypeOf(key);
    if (t != KeyType::None && t != KeyType::List)
        return Status::WrongType;
    if (t == KeyType::None)
        return Status::NotFound;
    out = static_cast<ssize_t>(list_store.find(key)->second.size());
    return Status::OK;
}

void RedisDatabase::lpush(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    removeIfExpired(key);
    list_store[key].insert(list_store[key].begin(), value);
}

void RedisDatabase::rpush(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    removeIfExpired(key);
    list_store[key].push_back(value);
}

bool RedisDatabase::lpop(const std::string &key, std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto it = list_store.find(key);

    if (it == list_store.end() || it->second.empty())
        return false;

    value = it->second.front();
    it->second.erase(it->second.begin());

    // 最后一个元素被弹出：删除 Key
    if (it->second.empty())
    {
        list_store.erase(it);

        bool stillExists = kv_store.find(key) != kv_store.end() ||
                           hash_store.find(key) != hash_store.end();
        if (!stillExists)
            expiry_map.erase(key);
    }
    return true;
}

bool RedisDatabase::rpop(const std::string &key, std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto it = list_store.find(key);

    if (it == list_store.end() || it->second.empty())
        return false;

    value = it->second.back();
    it->second.pop_back();

    if (it->second.empty())
    {
        list_store.erase(it);

        bool stillExists = kv_store.find(key) != kv_store.end() ||
                           hash_store.find(key) != hash_store.end();
        if (!stillExists)
            expiry_map.erase(key);
    }
    return true;
}

int RedisDatabase::lrem(const std::string &key, int count, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return 0;
    int removed = 0;
    auto it = list_store.find(key);
    if (it == list_store.end())
        return 0;

    auto &lst = it->second;

    if (count == 0)
    {

        auto new_end = std::remove(lst.begin(), lst.end(), value);
        removed = std::distance(new_end, lst.end());
        lst.erase(new_end, lst.end());
    }
    else if (count > 0)
    {

        for (auto iter = lst.begin(); iter != lst.end() && removed < count;)
        {
            if (*iter == value)
            {
                iter = lst.erase(iter);
                ++removed;
            }
            else
            {
                ++iter;
            }
        }
    }
    else
    {

        for (auto riter = lst.rbegin(); riter != lst.rend() && removed < (-count);)
        {
            if (*riter == value)
            {
                auto fwdIter = riter.base();
                --fwdIter;
                fwdIter = lst.erase(fwdIter);
                ++removed;
                riter = std::reverse_iterator<std::vector<std::string>::iterator>(fwdIter);
            }
            else
            {
                ++riter;
            }
        }
    }
    if (it->second.empty())
    {
        list_store.erase(it);

        bool stillExists = kv_store.find(key) != kv_store.end() ||
                           hash_store.find(key) != hash_store.end();

        if (!stillExists)
            expiry_map.erase(key);
    }
    return removed;
}

Status RedisDatabase::lindex(const std::string &key, int index, std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    removeIfExpired(key);

    KeyType t = keyTypeOf(key);
    if (t != KeyType::None && t != KeyType::List)
        return Status::WrongType;
    if (t == KeyType::None)
        return Status::NotFound;
    const auto &lst = list_store.find(key)->second;

    long long idx = static_cast<long long>(index);
    if (idx < 0)
        idx += static_cast<long long>(lst.size());
    if (idx < 0 || idx >= static_cast<long long>(lst.size()))
        return Status::NotFound;
    value = lst[static_cast<size_t>(idx)];
    return Status::OK;
}

bool RedisDatabase::lset(const std::string &key, int index, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto it = list_store.find(key);
    if (it == list_store.end())
        return false;

    auto &lst = it->second;
    if (index < 0)
        index = it->second.size() + index;
    if (index < 0 || index >= static_cast<int>(lst.size()))
        return false;

    lst[index] = value;
    return true;
}

// Hash Operations
bool RedisDatabase::hset(const std::string &key, const std::string &field, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    removeIfExpired(key);
    // 1. 查找外层 key
    auto keyIt = hash_store.find(key);
    // 2. 如果 key 不存在，创建 Hash 和 field
    if (keyIt == hash_store.end())
    {
        hash_store[key][field] = value;
        return true;
    }
    // 3. key 存在，查找内部 field
    auto fieldIt = keyIt->second.find(field);

    // 4. field 不存在，新增字段
    if (fieldIt == keyIt->second.end())
    {
        keyIt->second.emplace(field, value);
        return true;
    }
    // 5. field 已存在，只更新 value
    fieldIt->second = value;

    return false; // 表示没有新增field ，不是修改失败；
}

bool RedisDatabase::hget(const std::string &key, const std::string &field, std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto it = hash_store.find(key);

    if (it != hash_store.end())
    {
        auto f = it->second.find(field);
        if (f != it->second.end())
        {
            value = f->second;
            return true;
        }
    }
    return false;
}

bool RedisDatabase::hexists(const std::string &key, const std::string &field)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto it = hash_store.find(key);

    if (it != hash_store.end())
    {
        auto f = it->second.find(field);
        if (f != it->second.end())
            return true;
    }
    return false;
}

bool RedisDatabase::hdel(const std::string &key, const std::string &field)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return false;
    auto keyIt = hash_store.find(key);

    if (keyIt == hash_store.end())
        return false;

    auto fieldIt = keyIt->second.find(field);
    if (fieldIt == keyIt->second.end())
        return false;

    keyIt->second.erase(field);

    if (keyIt->second.empty())
    {
        hash_store.erase(keyIt);

        bool keyStillExists =
            kv_store.find(key) != kv_store.end() ||
            list_store.find(key) != list_store.end() ||
            hash_store.find(key) != hash_store.end();

        if (!keyStillExists)
            expiry_map.erase(key);
    }
    return true;
}

std::unordered_map<std::string, std::string> RedisDatabase::hgetall(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return {};
    auto it = hash_store.find(key);
    if (it != hash_store.end())
        return hash_store[key];
    return {};
}

std::vector<std::string> RedisDatabase::hkeys(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return {};
    std::vector<std::string> fields;
    auto it = hash_store.find(key);
    if (it != hash_store.end())
    {
        for (const auto &pair : it->second)
            fields.push_back(pair.first);
    }
    return fields;
}

std::vector<std::string> RedisDatabase::hvals(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return {};
    std::vector<std::string> values;

    auto it = hash_store.find(key);
    if (it != hash_store.end())
    {
        for (const auto &pair : it->second)
            values.push_back(pair.second);
    }
    return values;
}

ssize_t RedisDatabase::hlen(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    if (removeIfExpired(key))
        return 0;
    auto it = hash_store.find(key);
    if (it != hash_store.end())
        return it->second.size();
    return 0;
}

bool RedisDatabase::hmset(const std::string &key, const std::vector<std::pair<std::string, std::string>> &fieldValues)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    removeIfExpired(key);
    for (const auto &pair : fieldValues)
    {
        hash_store[key][pair.first] = pair.second;
    }
    return true;
}

bool RedisDatabase::dump(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ofstream ofs(filename, std::ios::binary);

    if (!ofs)
        return false;

    for (const auto &kv : kv_store)
    {
        ofs << "K" << kv.first << " " << kv.second << "\n";
    }

    for (const auto &kv : list_store)
    {
        ofs << "L" << kv.first;
        for (const auto &item : kv.second)
            ofs << " " << item;
        ofs << "\n";
    }

    for (const auto &kv : hash_store)
    {
        ofs << "H" << kv.first;
        for (const auto &field_val : kv.second)
            ofs << " " << field_val.first << ":" << field_val.second;
        ofs << "\n";
    }
    return true;
}

bool RedisDatabase::load(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    std::ifstream ifs(filename, std::ios::binary);

    if (!ifs)
        return false;

    kv_store.clear();
    list_store.clear();
    hash_store.clear();

    std::string Line;
    while (getline(ifs, Line))
    {
        std::istringstream iss(Line);
        char type = ' ';
        iss >> type;
        if (type == 'K')
        {
            std::string key, value;
            iss >> key >> value;
            kv_store[key] = value;
        }
        else if (type == 'L')
        {
            std::string key;
            iss >> key;
            std::string item;
            std::vector<std::string> list;
            while (iss >> item)
                list.push_back(item);
            list_store[key] = list;
        }
        else if (type == 'H')
        {
            std::string key;
            iss >> key;
            std::string pair;
            std::unordered_map<std::string, std::string> hash;
            while (iss >> pair)
            {
                auto pos = pair.find(':');
                if (pos != std::string::npos)
                {
                    std::string field = pair.substr(0, pos);
                    std::string value = pair.substr(pos + 1);
                    hash[field] = value;
                }
            }
            hash_store[key] = hash;
        }
    }
    return true;
}