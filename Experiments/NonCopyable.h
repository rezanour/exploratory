#pragma once

class NonCopyable
{
public:
    virtual ~NonCopyable() {}

protected:
    NonCopyable() {}

private:
    NonCopyable(const NonCopyable&);
    NonCopyable& operator= (const NonCopyable&);
};