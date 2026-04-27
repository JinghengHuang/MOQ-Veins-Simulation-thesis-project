
#pragma once
#include <vector>
#include "object.h"
/**
 * MOQ Subgroup, holds a group of frames(objects)
 */
class Subgroup
{
  private:
    std::vector<Object> frames;
    long id;
  public:
    Subgroup(long id, std::vector<Object> frames);
    ~Subgroup();

};