#include "StringUtils.h"
namespace moqveinssim

{
std::vector<std::string> StringUtils::splitString(std::string s, std::string del){
    std::vector<std::string> res;
    int pos = 0;
    while(pos < s.size()){
        pos = s.find(del);
        res.push_back(s.substr(0,pos));
        s.erase(0,pos + del.size());
    }
    // remaining part as last token
    res.push_back(s);
    return res;
}
} // namespace moqveinssim

