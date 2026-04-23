#include "camera.hpp"

namespace toy2d {

    Camera::Camera() {
        Reset();
    }

    void Camera::Reset() {
        viewMat_ = Mat4::CreateIdentity();
        projectMat_ = Mat4::CreateIdentity();
    }

    void Camera::SetProject(int right, int left, int bottom, int top, int far, int near) {
        projectMat_ = Mat4::CreateOrtho(left, right, top, bottom, near, far);
    }

    void Camera::SetView(const Mat4& view) {
        viewMat_ = view;
    }

}
