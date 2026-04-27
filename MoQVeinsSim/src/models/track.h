
#pragma once
#include <vector>
#include <string>
#include "group.h"
/**
 * MOQ Track, holds a group of groups, with a name.
 * Behaves like topic in pub/sub models
 */
class Track
{
  private:
    std::vector<Group> groups;
    std::string name;
  public:
    Track(std::vector<Group> groups, std::string name);
    ~Track();

};