#pragma once

// Common helper or utility classes, functions, and types

class NonCopyable
{
protected:
    NonCopyable() {}
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator= (const NonCopyable&) = delete;
};
