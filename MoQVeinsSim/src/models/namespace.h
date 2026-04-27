
#pragma once
#include <vector>
#include <string>
#include "track.h"
/**
 * MOQ Namespace, holds a group of Tracks, with a name.
 * Behaves like topic in pub/sub models
 */
class Namespace
{
  private:
    std::vector<Track> tracks;
    std::vector<std::string> namespaceId;
  public:
    Namespace(std::vector<Track> tracks, std::vector<std::string> name);
    ~Namespace();

};