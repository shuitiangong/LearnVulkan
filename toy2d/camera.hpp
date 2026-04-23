#pragma once

#include "math.hpp"

namespace toy2d {

    class Camera {
    public:
        Camera();

        void Reset();
        void SetProject(int right, int left, int bottom, int top, int far, int near);
        void SetView(const Mat4& view);

        const Mat4& GetProjectMatrix() const { return projectMat_; }
        const Mat4& GetViewMatrix() const { return viewMat_; }

    private:
        Mat4 projectMat_;
        Mat4 viewMat_;
    };

}
