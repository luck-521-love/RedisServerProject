#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <cstddef>

class Buffer
{
public:
    void Append(const char *data, size_t len);
    size_t readAble() const;
    const char *Peek() const;
    void Consume(size_t n);
    void Compact();
    void Clear();

private:
    std::vector<char> buf;
    size_t read_pos = 0;
};

#endif