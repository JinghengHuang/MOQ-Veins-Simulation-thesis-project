
#pragma once
#include <vector>
/**
 * MOQ object, holds a sized chunk of data
 */
class Object
{
  private:
    std::vector<char> content;
    long id;
  public:
    Object(long id, std::vector<char> content);
    ~Object();

};