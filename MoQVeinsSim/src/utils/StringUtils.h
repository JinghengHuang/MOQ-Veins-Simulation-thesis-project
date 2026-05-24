#pragma once
#include <vector>
#include <string>

namespace moqveinssim
{
    class StringUtils{
    public: 
        static std::vector<std::string> splitString(std::string s, std::string del);
    };
} // namespace moqveinssim
