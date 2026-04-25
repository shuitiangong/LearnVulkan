#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include "math.hpp"
#include "material.hpp"
#include "buffer.hpp"

namespace toy2d {

    struct Transform {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 size{1.0f, 1.0f, 1.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // radians

        void SetPosition(const glm::vec2& value) { position = glm::vec3(value, 0.0f); }
        void SetPosition(const glm::vec3& value) { position = value; }
        const glm::vec3& GetPosition() const { return position; }

        void SetSize(const glm::vec2& value) { size = glm::vec3(value, 1.0f); }
        void SetSize(const glm::vec3& value) { size = value; }
        const glm::vec3& GetSize() const { return size; }

        void SetScale(const glm::vec2& value) { scale = glm::vec3(value, 1.0f); }
        void SetScale(const glm::vec3& value) { scale = value; }
        const glm::vec3& GetScale() const { return scale; }

        void SetRotation(const glm::vec3& value) { rotation = value; }
        void SetRotationDegrees(const glm::vec3& value) { rotation = glm::radians(value); }
        void Rotate(const glm::vec3& delta) { rotation += delta; }
        void RotateDegrees(const glm::vec3& delta) { rotation += glm::radians(delta); }
        const glm::vec3& GetRotation() const { return rotation; }

        glm::mat4 GetModelMatrix() const {
            glm::vec3 finalScale{size.x * scale.x, size.y * scale.y, scale.z};
            glm::mat4 rotationMatrix(1.0f);
            rotationMatrix = glm::rotate(rotationMatrix, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
            rotationMatrix = glm::rotate(rotationMatrix, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            rotationMatrix = glm::rotate(rotationMatrix, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

            return glm::translate(glm::mat4(1.0f), position) *
                   rotationMatrix *
                   glm::scale(glm::mat4(1.0f), finalScale);
        }
    };

    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;

        void Upload();
    };

    inline Mesh CreateQuadMesh() {
        return Mesh{
            {
                Vertex{{-0.5f, -0.5f, 0.0f}, {0, 0}},
                Vertex{{0.5f, -0.5f, 0.0f}, {1, 0}},
                Vertex{{0.5f, 0.5f, 0.0f}, {1, 1}},
                Vertex{{-0.5f, 0.5f, 0.0f}, {0, 1}},
            },
            {0, 1, 3, 1, 2, 3}
        };
    }

    inline Mesh CreateCubeMesh() {
        return Mesh{
            {
                // front
                Vertex{{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
                Vertex{{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
                Vertex{{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
                Vertex{{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
                // back
                Vertex{{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
                Vertex{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
                Vertex{{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
                Vertex{{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
                // left
                Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
                Vertex{{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
                Vertex{{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
                Vertex{{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
                // right
                Vertex{{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
                Vertex{{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
                Vertex{{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
                Vertex{{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
                // top
                Vertex{{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},
                Vertex{{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
                Vertex{{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
                Vertex{{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
                // bottom
                Vertex{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
                Vertex{{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
                Vertex{{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
                Vertex{{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},
            },
            {
                0, 1, 2, 0, 2, 3,
                4, 5, 6, 4, 6, 7,
                8, 9, 10, 8, 10, 11,
                12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19,
                20, 21, 22, 20, 22, 23
            }
        };
    }

    class GameObject {
    public:
        Transform transform;
        Material* material = nullptr;
        Mesh* mesh = nullptr;
        bool visible = true;

        void SetPosition(const glm::vec2& position) { transform.SetPosition(position); }
        void SetPosition(const glm::vec3& position) { transform.SetPosition(position); }
        const glm::vec3& GetPosition() const { return transform.GetPosition(); }

        void SetSize(const glm::vec2& size) { transform.SetSize(size); }
        void SetSize(const glm::vec3& size) { transform.SetSize(size); }
        const glm::vec3& GetSize() const { return transform.GetSize(); }

        void SetScale(const glm::vec2& scale) { transform.SetScale(scale); }
        void SetScale(const glm::vec3& scale) { transform.SetScale(scale); }
        const glm::vec3& GetScale() const { return transform.GetScale(); }

        void SetRotation(const glm::vec3& rotation) { transform.SetRotation(rotation); }
        void SetRotationDegrees(const glm::vec3& rotation) { transform.SetRotationDegrees(rotation); }
        void Rotate(const glm::vec3& delta) { transform.Rotate(delta); }
        void RotateDegrees(const glm::vec3& delta) { transform.RotateDegrees(delta); }
        const glm::vec3& GetRotation() const { return transform.GetRotation(); }

        void SetMaterial(Material* newMaterial) { material = newMaterial; }
        Material* GetMaterial() { return material; }
        const Material* GetMaterial() const { return material; }

        void SetMesh(Mesh* newMesh) { mesh = newMesh; }
        Mesh* GetMesh() { return mesh; }
        const Mesh* GetMesh() const { return mesh; }

        void SetVisible(bool isVisible) { visible = isVisible; }
        bool IsVisible() const { return visible; }

        void SetColor(const glm::vec3& color) {
            if (material) {
                material->color = color;
            }
        }

        const glm::vec3* GetColor() const {
            return material ? &material->color : nullptr;
        }
    };

}
