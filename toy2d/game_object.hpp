#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "math.hpp"
#include "material.hpp"
#include "buffer.hpp"

namespace toy2d {

    struct Transform2D {
        Vec position{0, 0};
        Size size{1, 1};
        Vec scale{1, 1};

        void SetPosition(const Vec& value) { position = value; }
        const Vec& GetPosition() const { return position; }

        void SetSize(const Size& value) { size = value; }
        const Size& GetSize() const { return size; }

        void SetScale(const Vec& value) { scale = value; }
        const Vec& GetScale() const { return scale; }

        Mat4 GetModelMatrix() const {
            Vec finalScale{size.x * scale.x, size.y * scale.y};
            return Mat4::CreateTranslate(position).Mul(Mat4::CreateScale(finalScale));
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
                Vertex{{-0.5f, -0.5f}, {0, 0}},
                Vertex{{0.5f, -0.5f}, {1, 0}},
                Vertex{{0.5f, 0.5f}, {1, 1}},
                Vertex{{-0.5f, 0.5f}, {0, 1}},
            },
            {0, 1, 3, 1, 2, 3}
        };
    }

    class GameObject {
    public:
        Transform2D transform;
        Material* material = nullptr;
        Mesh* mesh = nullptr;
        bool visible = true;

        void SetPosition(const Vec& position) { transform.SetPosition(position); }
        const Vec& GetPosition() const { return transform.GetPosition(); }

        void SetSize(const Size& size) { transform.SetSize(size); }
        const Size& GetSize() const { return transform.GetSize(); }

        void SetScale(const Vec& scale) { transform.SetScale(scale); }
        const Vec& GetScale() const { return transform.GetScale(); }

        void SetMaterial(Material* newMaterial) { material = newMaterial; }
        Material* GetMaterial() { return material; }
        const Material* GetMaterial() const { return material; }

        void SetMesh(Mesh* newMesh) { mesh = newMesh; }
        Mesh* GetMesh() { return mesh; }
        const Mesh* GetMesh() const { return mesh; }

        void SetVisible(bool isVisible) { visible = isVisible; }
        bool IsVisible() const { return visible; }

        void SetColor(const Color& color) {
            if (material) {
                material->color = color;
            }
        }

        const Color* GetColor() const {
            return material ? &material->color : nullptr;
        }
    };

}
