#pragma once

#include "game_object.hpp"
#include <filesystem>

namespace toy2d {

    Mesh LoadObjMesh(const std::filesystem::path& path);

}
