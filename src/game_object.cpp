#include "game_object.hpp"

namespace toy2d {

    void Mesh::Upload() {
        auto vertexBufferSize = sizeof(Vertex) * vertices.size();
        auto indexBufferSize = sizeof(std::uint32_t) * indices.size();

        if (vertexBufferSize > 0) {
            if (!vertexBuffer || vertexBuffer->size < vertexBufferSize) {
                vertexBuffer = std::make_unique<Buffer>(vk::BufferUsageFlagBits::eVertexBuffer,
                                                        vertexBufferSize,
                                                        vk::MemoryPropertyFlagBits::eHostVisible |
                                                        vk::MemoryPropertyFlagBits::eHostCoherent);
            }
            vertexBuffer->WriteData(vertices.data(), vertexBufferSize);
        }

        if (indexBufferSize > 0) {
            if (!indexBuffer || indexBuffer->size < indexBufferSize) {
                indexBuffer = std::make_unique<Buffer>(vk::BufferUsageFlagBits::eIndexBuffer,
                                                       indexBufferSize,
                                                       vk::MemoryPropertyFlagBits::eHostVisible |
                                                       vk::MemoryPropertyFlagBits::eHostCoherent);
            }
            indexBuffer->WriteData(indices.data(), indexBufferSize);
        }
    }

}
