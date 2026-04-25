#include "model.hpp"

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace toy2d {

    namespace {

        glm::vec3 ReadPosition(const tinyobj::attrib_t& attrib, int index) {
            auto offset = static_cast<std::size_t>(index) * 3;
            if (offset + 2 >= attrib.vertices.size()) {
                throw std::runtime_error("OBJ vertex position index is out of range");
            }

            return {
                attrib.vertices[offset + 0],
                attrib.vertices[offset + 1],
                attrib.vertices[offset + 2]
            };
        }

        glm::vec2 ReadTexcoord(const tinyobj::attrib_t& attrib, int index) {
            if (index < 0 || attrib.texcoords.empty()) {
                return {0.0f, 0.0f};
            }

            auto offset = static_cast<std::size_t>(index) * 2;
            if (offset + 1 >= attrib.texcoords.size()) {
                throw std::runtime_error("OBJ texture coordinate index is out of range");
            }

            return {
                attrib.texcoords[offset + 0],
                1.0f - attrib.texcoords[offset + 1]
            };
        }

    }

    Mesh LoadObjMesh(const std::filesystem::path& path) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warning;
        std::string error;

        auto objectPath = path.lexically_normal();
        auto materialDir = objectPath.parent_path().string();
        if (!materialDir.empty()) {
            materialDir += std::filesystem::path::preferred_separator;
        }

        bool loaded = tinyobj::LoadObj(&attrib,
                                       &shapes,
                                       &materials,
                                       &warning,
                                       &error,
                                       objectPath.string().c_str(),
                                       materialDir.empty() ? nullptr : materialDir.c_str(),
                                       true);

        if (!warning.empty()) {
            std::clog << "tinyobjloader warning: " << warning << '\n';
        }

        if (!loaded) {
            std::ostringstream message;
            message << "failed to load OBJ model: " << objectPath.string();
            if (!error.empty()) {
                message << " (" << error << ")";
            }
            throw std::runtime_error(message.str());
        }

        Mesh mesh;
        std::unordered_map<Vertex, std::uint32_t> uniqueVertices;
        uniqueVertices.reserve(attrib.vertices.size() / 3);

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                if (index.vertex_index < 0) {
                    throw std::runtime_error("OBJ contains a face without vertex position");
                }

                Vertex vertex{};
                vertex.position = ReadPosition(attrib, index.vertex_index);
                vertex.texcoord = ReadTexcoord(attrib, index.texcoord_index);

                auto [it, inserted] =
                    uniqueVertices.try_emplace(vertex, static_cast<std::uint32_t>(mesh.vertices.size()));
                if (inserted) {
                    mesh.vertices.push_back(vertex);
                }

                mesh.indices.push_back(it->second);
            }
        }

        if (mesh.vertices.empty() || mesh.indices.empty()) {
            throw std::runtime_error("OBJ model did not contain any triangles");
        }

        return mesh;
    }

}
