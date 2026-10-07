#pragma once

#include <string>

class Content;
class Model;

// Room_<level>_water.wmh (or .wml). Null when the level has no water file.
// The returned model owns one mesh per chunk. The caller deletes it with the level.
Model* loadTy1WaterModel(Content& content, const std::string& levelFile);
