#include "../include/Buffer.h"
#include <cassert>
#include <cstring>

void Buffer::Append(const char *data, size_t len)
{
    if (len == 0)
        return;
    buf.insert(buf.end(), data, data + len);
}

size_t Buffer::readAble() const
{
    return buf.size() - read_pos;
}

const char *Buffer::Peek() const
{
    return buf.data() + read_pos;
}

void Buffer::Consume(size_t n)
{
    if (n == 0)
        return;
    assert(n <= readAble());
    read_pos += n;

    if (read_pos == buf.size())
    {
        Clear();
    }
    else if (read_pos > buf.size() / 2)
    {
        Compact();
    }
}

void Buffer::Compact()
{
    size_t remain = readAble();
    if (remain == 0)
    {
        Clear();
        return;
    }

    std::memmove(buf.data(), buf.data() + read_pos, remain);
    buf.resize(remain);
    read_pos = 0;
}

void Buffer::Clear()
{
    buf.clear();
    read_pos = 0;
}