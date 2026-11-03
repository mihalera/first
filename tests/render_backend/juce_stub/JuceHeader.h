#pragma once

namespace juce
{
class String
{
public:
    String (const char*) {}

    String& operator<< (const char*)
    {
        return *this;
    }
};
}
