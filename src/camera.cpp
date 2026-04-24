#include "camera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace toy2d {

    namespace {
        constexpr float kDefaultYaw = -90.0f;
        constexpr float kDefaultPitch = 0.0f;
        constexpr float kDefaultSpeed = 3.5f;
        constexpr float kDefaultSensitivity = 0.1f;
        constexpr float kDefaultZoom = 45.0f;
        constexpr float kDefaultAspectRatio = 16.0f / 9.0f;
        constexpr float kDefaultNearPlane = 0.1f;
        constexpr float kDefaultFarPlane = 100.0f;
    }

    Camera::Camera() {
        Reset();
    }

    void Camera::Reset() {
        position_ = glm::vec3(0.0f, 0.0f, 5.0f);
        front_ = glm::vec3(0.0f, 0.0f, -1.0f);
        worldUp_ = glm::vec3(0.0f, 1.0f, 0.0f);
        yaw_ = kDefaultYaw;
        pitch_ = kDefaultPitch;
        movementSpeed_ = kDefaultSpeed;
        mouseSensitivity_ = kDefaultSensitivity;
        zoom_ = kDefaultZoom;
        aspectRatio_ = kDefaultAspectRatio;
        nearPlane_ = kDefaultNearPlane;
        farPlane_ = kDefaultFarPlane;

        updateCameraVectors();
        syncProjectionMatrix();
    }

    void Camera::SetPerspective(float fovDegrees, float aspectRatio, float nearPlane, float farPlane) {
        zoom_ = std::clamp(fovDegrees, 1.0f, 45.0f);
        aspectRatio_ = std::max(aspectRatio, 0.0001f);
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
        syncProjectionMatrix();
    }

    void Camera::SetView(const glm::mat4& view) {
        viewMat_ = view;
    }

    void Camera::SetPosition(const glm::vec3& position) {
        SetPosition(position.x, position.y, position.z);
    }

    void Camera::SetPosition(float x, float y, float z) {
        position_ = glm::vec3(x, y, z);
        syncViewMatrix();
    }

    glm::vec3 Camera::GetPosition() const {
        return position_;
    }

    void Camera::ProcessKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = movementSpeed_ * deltaTime;
        switch (direction) {
        case CameraMovement::Forward:
            position_ += front_ * velocity;
            break;
        case CameraMovement::Backward:
            position_ -= front_ * velocity;
            break;
        case CameraMovement::Left:
            position_ -= right_ * velocity;
            break;
        case CameraMovement::Right:
            position_ += right_ * velocity;
            break;
        case CameraMovement::Up:
            position_ += worldUp_ * velocity;
            break;
        case CameraMovement::Down:
            position_ -= worldUp_ * velocity;
            break;
        }

        syncViewMatrix();
    }

    void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
        xoffset *= mouseSensitivity_;
        yoffset *= mouseSensitivity_;

        yaw_ += xoffset;
        pitch_ += yoffset;

        if (constrainPitch) {
            pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
        }

        updateCameraVectors();
    }

    void Camera::ProcessMouseScroll(float yoffset) {
        zoom_ = std::clamp(zoom_ - yoffset, 1.0f, 45.0f);
        syncProjectionMatrix();
    }

    void Camera::syncProjectionMatrix() {
        projectMat_ = glm::perspectiveRH_ZO(glm::radians(zoom_), aspectRatio_, nearPlane_, farPlane_);
        projectMat_[1][1] *= -1.0f;
    }

    void Camera::syncViewMatrix() {
        viewMat_ = glm::lookAt(position_, position_ + front_, up_);
    }

    void Camera::updateCameraVectors() {
        glm::vec3 front;
        front.x = std::cos(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
        front.y = std::sin(glm::radians(pitch_));
        front.z = std::sin(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
        front_ = glm::normalize(front);
        right_ = glm::normalize(glm::cross(front_, worldUp_));
        up_ = glm::normalize(glm::cross(right_, front_));
        syncViewMatrix();
    }

}
