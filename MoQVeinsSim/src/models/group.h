
#pragma once
#include <vector>
#include "subgroup.h"
/**
 * MOQ Group, holds a group of subgroups
 */
class Group
{
  private:
    std::vector<Subgroup> subgroups;
    long id;
  public:
    Group(long id, std::vector<Subgroup> subgroups);
    ~Group();

};