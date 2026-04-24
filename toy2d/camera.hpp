#pragma once

#include "math.hpp"
#include <glm/glm.hpp>

namespace toy2d {

    enum class CameraMovement {
        Forward,
        Backward,
        Left,
        Right,
        Up,
        Down
    };

    class Camera {
    public:
        Camera();

        void Reset();
        void SetPerspective(float fovDegrees, float aspectRatio, float nearPlane, float farPlane);
        void SetView(const glm::mat4& view);
        void SetPosition(const glm::vec3& position);
        void SetPosition(float x, float y, float z);
        glm::vec3 GetPosition() const;
        float GetZoom() const { return zoom_; }
        void ProcessKeyboard(CameraMovement direction, float deltaTime);
        void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
        void ProcessMouseScroll(float yoffset);

        const glm::mat4& GetProjectMatrix() const { return projectMat_; }
        const glm::mat4& GetViewMatrix() const { return viewMat_; }

    private:
        glm::vec3 position_;
        glm::vec3 front_;
        glm::vec3 up_;
        glm::vec3 right_;
        glm::vec3 worldUp_;
        float yaw_;
        float pitch_;
        float movementSpeed_;
        float mouseSensitivity_;
        float zoom_;
        float aspectRatio_;
        float nearPlane_;
        float farPlane_;
        glm::mat4 projectMat_;
        glm::mat4 viewMat_;

        void syncProjectionMatrix();
        void syncViewMatrix();
        void updateCameraVectors();
    };

}
