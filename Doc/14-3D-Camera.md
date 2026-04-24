# 1 Vulkan 中的 3D Camera

## 1.1 概念

### 1.1.1 Camera / View Space 是什么

Camera 在渲染里是一套观察规则。  
它的核心作用是把世界空间中的顶点，变换到“以相机为原点”的观察空间。

当前工程的顶点变换链路仍然是标准的 MVP：

```glsl
layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 project;
    mat4 view;
    mat4 model;
} ubo;

void main() {
    gl_Position = ubo.project * ubo.view * ubo.model * vec4(inPosition, 1.0);
    outTexcoord = inTexcoord;
}
```

这条链路可以概括为：

```text
local space
    -> model
    -> world space
    -> view
    -> view space
    -> projection
    -> clip space
```

其中：

1. `model` 决定物体在世界里的位置、缩放和旋转；
2. `view` 决定“相机站在哪里、朝哪里看”；
3. `projection` 决定观察体积如何压缩到裁剪空间。

所以 Camera 的本质不是“移动摄像机物体”，而是持续生成 `view` 和 `projection` 两个矩阵。

---

### 1.1.2 当前工程里的 3D Camera 状态管理

如果只有一个透视投影矩阵，而没有位置、朝向和输入更新逻辑，那么它只能算“固定透视视角”，还不能算可交互的 3D Camera。

当前工程里的 `Camera` 状态如下：

```cpp
class Camera {
public:
    void SetPerspective(float fovDegrees, float aspectRatio, float nearPlane, float farPlane);
    void SetPosition(float x, float y, float z);
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
};
```

这组状态对应 LearnOpenGL 教程里 Camera 的常见抽象：

1. `position_`：相机位置；
2. `front_`：相机朝向；
3. `right_ / up_`：相机局部坐标系；
4. `yaw_ / pitch_`：方向控制参数；
5. `zoom_`：FOV；
6. `aspectRatio_ / nearPlane_ / farPlane_`：透视投影参数。

### 1.1.3 Vulkan Camera 和 OpenGL Camera 的差别

Camera 的空间几何关系本身并不会因为 API 不同而变化。  
真正的差别出在投影矩阵对应的裁剪空间约定上。

当前工程需要处理两件事：

1. Vulkan 深度范围是 `[0, 1]`
2. 画面 Y 方向需要翻转到 Vulkan 约定

当前工程没有直接使用 `glm::perspectiveRH_ZO`，而是手写了一个投影矩阵：

```cpp
glm::mat4 CreatePerspectiveRH_ZO(float fovYRadians,
                                 float aspectRatio,
                                 float nearPlane,
                                 float farPlane) {
    float tanHalfFovY = std::tan(fovYRadians * 0.5f);
    glm::mat4 matrix(0.0f);

    matrix[0][0] = 1.0f / (aspectRatio * tanHalfFovY);
    matrix[1][1] = -1.0f / tanHalfFovY;
    matrix[2][2] = farPlane / (nearPlane - farPlane);
    matrix[2][3] = -1.0f;
    matrix[3][2] = (nearPlane * farPlane) / (nearPlane - farPlane);

    return matrix;
}
```

这里最关键的是：

1. `matrix[2][2]` 和 `matrix[3][2]`
   - 把深度映射到 Vulkan 的 `[0, 1]`
2. `matrix[1][1] = -1.0f / tanHalfFovY`
   - 直接把 Y 翻转写进投影矩阵

Y 翻转带来的直接副作用是：  
三角形在屏幕空间的绕序会跟着翻转，所以 Rasterizer 的 `FrontFace` 也要和这套投影约定保持一致。

## 1.2 Camera / View Space

### 1.2.1 View Matrix 与“反向移动世界”

LearnOpenGL 里对 Camera 的一个关键解释是：  
OpenGL/Vulkan 都不会“帮忙移动一个摄像机对象”，而是把整个世界按相机的反方向移动。

这件事在矩阵层面的体现，就是 `view matrix`：

```text
相机向前走
    -> 世界看起来向后退
相机向右看
    -> 世界看起来向左转
```

当前工程没有直接手写 view matrix 的每个元素，而是用 `glm::lookAt` 构造：

```cpp
void Camera::syncViewMatrix() {
    viewMat_ = glm::lookAt(position_, position_ + front_, up_);
}
```

`lookAt` 的三个输入分别代表：

1. `position_`：相机位置；
2. `position_ + front_`：观察目标点；
3. `up_`：相机当前上方向。

只要这三个量是稳定的，view matrix 就能稳定表示“世界相对于相机的变换”。

---

### 1.2.2 当前工程里的默认相机初始化

当前工程在 `Reset()` 里给出了默认的 Camera 状态：

```cpp
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
```

其中：

```cpp
constexpr float kDefaultYaw = -90.0f;
constexpr float kDefaultPitch = 0.0f;
```

`yaw = -90°` 的作用是让默认朝向落在 `-Z` 方向。  
这和当前工程中“相机从正 Z 方向看向场景”的布局是一致的。

## 1.3 Look At

### 1.3.1 LookAt 矩阵的作用

LookAt 矩阵的思想和 LearnOpenGL 教程保持一致：  
只要拿到相机局部坐标系的三个正交方向，再加上相机位置，就可以构造一个观察矩阵。

这三个方向是：

1. 前方向 `front`
2. 右方向 `right`
3. 上方向 `up`

当前工程没有自己拼出完整 LookAt 矩阵，而是把这部分交给 GLM：

```cpp
viewMat_ = glm::lookAt(position_, position_ + front_, up_);
```

这并不影响 Camera 的核心结构，因为真正要维护的仍然是：

1. `position_`
2. `front_`
3. `up_`
4. `right_`

也就是说，Camera 实现的关键不是“有没有自己手写 LookAt”，而是有没有完整维护这组向量关系。

---

### 1.3.2 当前工程里的相机朝向更新

当前工程使用 `updateCameraVectors()` 维护 `front_ / right_ / up_`：

```cpp
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
```

这段逻辑和 LearnOpenGL 教程里的 Camera 向量更新思路一致：

1. 先由 `yaw / pitch` 求 `front`
2. 再通过叉积求 `right`
3. 最后再通过叉积求 `up`

这里的 `worldUp_` 只是“世界参考上方向”，并不是最终用于 `lookAt` 的局部 `up_`。  
这两个概念不能混用。

## 1.4 键盘移动

### 1.4.1 键盘移动与相机位置更新

LearnOpenGL 教程里的做法是根据按键沿 `front` 和 `right` 方向移动。  
当前工程也采用了同样思路，只是额外保留了 `Up / Down` 两个方向：

```cpp
enum class CameraMovement {
    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down
};
```

位移代码如下：

```cpp
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
```

这里的关键点不是 `switch` 分支，而是：

```cpp
float velocity = movementSpeed_ * deltaTime;
```

### 1.4.2 `deltaTime` 的作用

LearnOpenGL 教程里专门强调了 `deltaTime`。  
原因很简单：如果不乘时间差，相机速度会直接绑定到帧率。

当前工程在主循环中用 SDL 的高精度计时器计算 `deltaTime`：

```cpp
Uint64 currentCounter = SDL_GetPerformanceCounter();
float deltaTime = static_cast<float>(currentCounter - lastCounter) /
                  static_cast<float>(SDL_GetPerformanceFrequency());
lastCounter = currentCounter;
```

然后再把键盘状态映射到 Camera：

```cpp
const bool* keyboardState = SDL_GetKeyboardState(nullptr);
if (keyboardState[SDL_SCANCODE_W]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Forward, deltaTime);
}
if (keyboardState[SDL_SCANCODE_S]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Backward, deltaTime);
}
if (keyboardState[SDL_SCANCODE_A]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Left, deltaTime);
}
if (keyboardState[SDL_SCANCODE_D]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Right, deltaTime);
}
if (keyboardState[SDL_SCANCODE_Q]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Up, deltaTime);
}
if (keyboardState[SDL_SCANCODE_E]) {
    camera.ProcessKeyboard(toy2d::CameraMovement::Down, deltaTime);
}
```

这一步完成之后，相机移动速度就变成“每秒固定移动距离”，而不是“每帧固定移动距离”。

## 1.5 鼠标视角

### 1.5.1 鼠标视角与方向更新

LearnOpenGL 教程在 Camera 一节里把鼠标输入先转成欧拉角，再由欧拉角转成方向向量。  
这条路径的可维护性最好，也最容易做俯仰角限制。

当前工程也遵循这个结构：

```cpp
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
```

流程是：

```text
鼠标位移
    -> yaw / pitch
    -> front / right / up
    -> view matrix
```

这比“每次直接修改 view matrix 元素”稳定得多。

---

### 1.5.2 pitch 限制

当前工程保留了 LearnOpenGL 教程里同样的限制：

```cpp
pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
```

原因是当 `pitch` 接近 `±90°` 时，前方向会与世界上方向接近平行，叉积求出来的 `right_` 会非常不稳定。  
结果通常表现为视角翻转、抖动或者“滚过去”的感觉。

---

### 1.5.3 当前工程里的鼠标输入接入

当前工程没有永久捕获鼠标，而是只在按住鼠标右键时才允许旋转视角：

```cpp
if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
    rotateCamera = true;
}
if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
    rotateCamera = false;
}
if (event.type == SDL_EVENT_MOUSE_MOTION && rotateCamera) {
    camera.ProcessMouseMovement(static_cast<float>(event.motion.xrel),
                                static_cast<float>(-event.motion.yrel));
}
```

这里对 `yrel` 取负号，是为了让：

1. 鼠标上移时视角抬头
2. 鼠标下移时视角低头

这和 FPS 相机的直觉是一致的。

## 1.6 缩放

### 1.6.1 zoom 与 FOV

Camera 一节里滚轮缩放并不是“把相机沿前方向移动”，而是修改视野角。  
当前工程也沿用了这套做法：

```cpp
void Camera::ProcessMouseScroll(float yoffset) {
    zoom_ = std::clamp(zoom_ - yoffset, 1.0f, 45.0f);
    syncProjectionMatrix();
}
```

这里修改的是投影矩阵，不是 view matrix。

效果是：

1. `zoom_` 变小，FOV 变窄，画面看起来更“拉近”
2. `zoom_` 变大，FOV 变宽，画面看起来更“拉远”

### 1.6.2 当前工程里的透视投影生成

当前工程对外暴露的接口是：

```cpp
void Camera::SetPerspective(float fovDegrees, float aspectRatio, float nearPlane, float farPlane) {
    zoom_ = std::clamp(fovDegrees, 1.0f, 45.0f);
    aspectRatio_ = std::max(aspectRatio, 0.0001f);
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;
    syncProjectionMatrix();
}
```

真正生成矩阵的代码是：

```cpp
void Camera::syncProjectionMatrix() {
    projectMat_ = CreatePerspectiveRH_ZO(glm::radians(zoom_),
                                         aspectRatio_,
                                         nearPlane_,
                                         farPlane_);
}
```

初始化阶段则把窗口宽高比传给 Camera：

```cpp
renderer_->GetCamera().SetPerspective(45.0f,
                                      static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
                                      0.1f,
                                      100.0f);
```

这部分结构和 LearnOpenGL 教程的 zoom/FOV 组织方式是一致的，只是投影矩阵实现改成了当前工程自己维护。

## 1.7 Camera Class

### 1.7.1 Camera 类的职责

LearnOpenGL Camera 教程的最后一步是把位置、方向、输入和矩阵更新全部收敛进一个类。  
当前工程也已经走到了这一步。

这样做的好处很直接：

1. `view` 和 `projection` 的更新逻辑集中；
2. 键盘、鼠标、滚轮输入都只操作 Camera；
3. 渲染器只关心“拿矩阵”，不关心相机内部细节。

当前工程里，渲染器只在写 UBO 时读取 Camera 结果：

```cpp
std::uint32_t Renderer::bufferMVPData(const glm::mat4& model) {
    MVP mvpData;
    mvpData.project = camera_.GetProjectMatrix();
    mvpData.view = camera_.GetViewMatrix();
    mvpData.model = model;

    auto offset = alignedMvpSize_ * objectCountInFrame_;
    mvpUniformBuffers_[curFrame_]->WriteData(&mvpData, sizeof(mvpData), offset);
    ++objectCountInFrame_;
    return offset;
}
```

这意味着 Camera 已经成为独立的上层控制模块，Renderer 只消费它输出的矩阵数据。

---

### 1.7.2 当前 Camera 的完成度与边界

当前工程的 Camera 已经具备：

1. `view matrix`
2. `projection matrix`
3. 键盘位移
4. 鼠标视角
5. 滚轮缩放
6. Vulkan 风格投影矩阵

这已经满足 LearnOpenGL Camera 一节想表达的主体结构。

但从整个工程角度看，仍然有两个边界需要明确：

1. Camera 已经完整，不代表整个渲染器的所有 3D 能力都已经完整
2. Camera 负责观察与投影，depth buffer 负责可见性裁决，这两部分是分开的

当前工程里，depth buffer 已经实现完成，相关代码分别位于：

1. `Swapchain::createDepthResource()`  
   负责创建深度 image、memory 和 image view；
2. `RenderProcess::createRenderPass()`  
   负责把 depth attachment 接入 render pass；
3. `RenderProcess::createGraphicsPipeline()`  
   负责开启 `depthTestEnable`、`depthWriteEnable` 和 `vk::CompareOp::eLess`；
4. `Renderer::BeginFrame()`  
   负责在每帧开始时把深度清到 `1.0f`。

也就是说，当前工程已经不是“只有 3D Camera、还没有 depth”的状态，而是：

1. Camera 已经负责生成 `view / projection`
2. 渲染管线已经负责执行 depth test
3. 多个 cube 的遮挡关系已经可以由深度缓冲决定

后续如果继续推进 3D 渲染，更合理的扩展方向会变成：

1. resize 时重新创建 swapchain 与 depth attachment
2. 视口变化时同步更新 Camera 的 `aspectRatio`
3. 继续补旋转、法线、光照等真正 3D 物体表现

## 1.8 常见误区

### 1.8.1 只改透视矩阵，不维护 view matrix

这样最多只会得到一个固定透视视角，得不到真正可移动的 Camera。

### 1.8.2 直接改矩阵，不维护 yaw / pitch / front

短期看起来能跑，后面会很难继续做鼠标输入约束和方向同步。

### 1.8.3 忘记 `deltaTime`

这样相机速度会绑定帧率。

### 1.8.4 忽略 Vulkan 的投影约定

如果直接搬 OpenGL 风格投影矩阵，常见后果是：

1. Y 方向颠倒
2. 深度范围不匹配
3. 面绕序判断和剔除状态对不上

### 1.8.5 把 Camera 完成度和渲染器完成度混为一谈

当前工程已经有完整 Camera，也已经接入了 depth buffer。  
但 Camera、depth、光照、材质、模型数据仍然是不同层级的问题：

1. Camera 决定怎么看；
2. depth 决定谁挡住谁；
3. 光照决定明暗；
4. 材质和纹理决定表面表现。

把这些模块拆开理解，会比把它们混成一个“3D 功能是否做完”的大概念更清晰。
