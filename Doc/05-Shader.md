# 1 Shader

## 1.1 概念

Shader 是运行在 GPU 可编程管线阶段上的小程序。  
在 Vulkan 中，shader 不直接以 GLSL/HLSL 源码形式交给驱动使用，而是通常先离线编译成 SPIR-V，再由应用在运行时创建 `vk::ShaderModule`，最后把 shader module 绑定到 graphics pipeline 或 compute pipeline 的指定阶段。

可以把 Vulkan shader 链路理解成：

```text
GLSL / HLSL 源码
    -> 编译为 SPIR-V
    -> vk::ShaderModule
    -> vk::PipelineShaderStageCreateInfo
    -> Graphics Pipeline / Compute Pipeline
```

Vulkan 这样设计的核心原因是：驱动不再必须在运行时解析高级 shader 源码，而是接收一种标准中间表示 SPIR-V。  
这带来几个好处：

1. 应用可以在构建阶段提前发现 shader 编译错误；
2. 运行时加载速度更稳定；
3. GLSL、HLSL 等不同语言可以统一编译到 SPIR-V；
4. 驱动面对的是更明确的中间表示，跨平台一致性更好。

在 graphics pipeline 中，最常见的 shader 阶段包括：

1. **Vertex Shader**：处理顶点，输出裁剪空间位置和传给后续阶段的 varying；
2. **Fragment Shader**：处理片元，输出颜色、深度等结果；
3. **Geometry Shader**：可选阶段，可以在图元级别生成或修改图元；
4. **Tessellation Control / Evaluation Shader**：可选阶段，用于曲面细分；
5. **Compute Shader**：不属于 graphics pipeline，用于通用计算任务。

当前项目只使用最基础的两个阶段：

1. `shader.vert`：vertex shader，生成一个三角形的顶点位置；
2. `shader.frag`：fragment shader，输出固定红色。

常见误区：

1. **以为 Vulkan 直接加载 `.vert` / `.frag` 源码**：Vulkan API 接收的是 SPIR-V 二进制，不是 GLSL 文本。
2. **把 `vk::ShaderModule` 当成可独立执行对象**：shader module 只是 shader 代码容器，真正执行发生在 pipeline 中。
3. **忘记 shader 入口名**：`vk::PipelineShaderStageCreateInfo::setPName("main")` 必须匹配 shader 中的入口函数。
4. **shader 输入输出 location 不匹配**：vertex shader 输出和 fragment shader 输入需要通过 `layout(location = N)` 对齐。
5. **创建 pipeline 后就立刻销毁 shader module 的时机不清楚**：pipeline 创建完成后，shader module 通常可以销毁；但在当前项目中为了管理简单，`Shader` 对象持续持有 module，到退出时统一销毁。

## 1.2 编译流程

### 1.2.1 编写 Vertex Shader：`shader.vert`

目标：定义顶点阶段如何产生顶点位置。  
当前项目没有创建 vertex buffer，而是直接在 vertex shader 里写死三个顶点，并用 `gl_VertexIndex` 选择当前顶点。

```glsl
#version 450

vec2 position[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    gl_Position = vec4(position[gl_VertexIndex], 0.0, 1.0);
}
```

`gl_VertexIndex` 是 Vulkan GLSL 中可用的内建变量，表示当前顶点的索引。  
当命令缓冲中调用：

```cpp
cmdBuf.draw(3, 1, 0, 0);
```

GPU 会执行 3 次 vertex shader，`gl_VertexIndex` 分别为 0、1、2，从数组中取出三个位置，最终组成一个三角形。

`gl_Position` 是 vertex shader 的标准输出，表示裁剪空间坐标。  
它是一个 `vec4`，后续会经过透视除法、视口变换，最终映射到 framebuffer 上。

---

### 1.2.2 编写 Fragment Shader：`shader.frag`

目标：定义片元阶段输出什么颜色。  
当前项目的 fragment shader 直接输出固定红色。

```glsl
#version 450

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
```

`layout(location = 0) out vec4 fragColor` 表示 fragment shader 的第 0 号颜色输出。  
它会对应 render pass 中 subpass 的 color attachment。

当前 render pass 只有一个颜色附件，所以 location 0 就是写入 swapchain image 的颜色。  
如果后续使用 MRT（Multiple Render Targets），fragment shader 可以声明多个输出：

```glsl
layout(location = 0) out vec4 albedo;
layout(location = 1) out vec4 normal;
```

此时 render pass / subpass / framebuffer 也需要提供对应数量的 color attachments。

---

### 1.2.3 编译为 SPIR-V：`glslc`

目标：把 GLSL 源码编译成 Vulkan 可加载的 SPIR-V 二进制。  
项目的 CMake 会查找 Vulkan SDK 中的 `glslc`，并把 `shader/*.vert`、`shader/*.frag` 编译为 `.spv` 文件。

手动命令形式通常是：

```powershell
glslc shader/shader.vert -o shader/shader.vert.spv
glslc shader/shader.frag -o shader/shader.frag.spv
```

项目中的 `cmake/compile_glsl.cmake` 会执行类似逻辑：

```cmake
execute_process(
    COMMAND "${GLSLC_EXECUTABLE}" "${shader_src}" -o "${shader_out}"
    RESULT_VARIABLE glslc_result
    OUTPUT_VARIABLE glslc_stdout
    ERROR_VARIABLE glslc_stderr
)
```

编译产物不是文本文件，而是二进制 SPIR-V。  
因此运行时读取 `.spv` 时要按二进制方式读取，并且大小应当是 `uint32_t` 的整数倍。

## 1.3 运行时加载流程

### 1.3.1 读取 SPIR-V 文件：`ReadSpvFile`

目标：把 `.spv` 文件读取成 `std::vector<uint32_t>`。  
Vulkan 的 `vk::ShaderModuleCreateInfo::pCode` 要求指向 32-bit word 数组，因此项目没有使用 `std::string` 保存 SPIR-V，而是使用 `std::vector<uint32_t>`。

```cpp
inline std::vector<uint32_t> ReadSpvFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        return {};
    }

    const auto size = file.tellg();
    if (size <= 0 ||
        (static_cast<std::size_t>(size) % sizeof(uint32_t)) != 0) {
        return {};
    }

    std::vector<uint32_t> content(
        static_cast<std::size_t>(size) / sizeof(uint32_t)
    );

    file.seekg(0);
    file.read(reinterpret_cast<char*>(content.data()), size);
    return content;
}
```

这里有两个关键点：

1. `std::ios::binary`：必须按二进制读取；
2. 文件大小必须是 `sizeof(uint32_t)` 的整数倍，因为 SPIR-V 按 32-bit word 组织。

当前项目在初始化时读取：

```cpp
Shader::Init(
    "shader/shader.vert.spv",
    "shader/shader.frag.spv"
);
```

这些文件由构建系统编译并复制到运行目录的 `shader` 文件夹中。

---

### 1.3.2 创建 Shader Module：`vk::Device::createShaderModule`

目标：把 SPIR-V 二进制交给 Vulkan，创建可被 pipeline 使用的 shader module。

```cpp
vk::ShaderModuleCreateInfo createInfo;
createInfo.codeSize = vertexSource.size() * sizeof(uint32_t);
createInfo.pCode = vertexSource.data();

vk::ShaderModule vertexModule =
    device.createShaderModule(createInfo);
```

fragment shader 也使用同样流程：

```cpp
createInfo.codeSize = fragmentSource.size() * sizeof(uint32_t);
createInfo.pCode = fragmentSource.data();

vk::ShaderModule fragmentModule =
    device.createShaderModule(createInfo);
```

`vk::ShaderModule` 属于 device 级对象，因此创建和销毁都通过 `vk::Device` 完成。  
它只负责保存一段已编译 shader 代码，不包含“这是 vertex shader 还是 fragment shader”的信息。shader 阶段是在下一步 `vk::PipelineShaderStageCreateInfo` 中指定的。

---

### 1.3.3 指定 Pipeline Shader Stage：`vk::PipelineShaderStageCreateInfo`

目标：告诉 graphics pipeline：哪个 shader module 用于哪个阶段，入口函数叫什么。

```cpp
std::vector<vk::PipelineShaderStageCreateInfo> stages(2);

stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
         .setModule(vertexModule)
         .setPName("main");

stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
         .setModule(fragmentModule)
         .setPName("main");
```

`setStage` 决定 shader 所属阶段：

1. `eVertex`：顶点阶段；
2. `eFragment`：片元阶段；
3. `eCompute`：计算阶段；
4. `eGeometry`：几何阶段；
5. `eTessellationControl / eTessellationEvaluation`：曲面细分阶段。

`setPName("main")` 表示入口函数名。  
它必须和 shader 源码中实际入口一致：

```glsl
void main() {
    // ...
}
```

当前项目的 `Shader::initStage()` 正是这样组织两个阶段：

```cpp
void Shader::initStage() {
    stage_.resize(2);
    stage_[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vertexModule)
             .setPName("main");
    stage_[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(fragmentModule)
             .setPName("main");
}
```

---

### 1.3.4 写入 Graphics Pipeline：`vk::GraphicsPipelineCreateInfo::setStages`

目标：把 shader stages 纳入 graphics pipeline 创建信息。  
shader stage 是 pipeline 的一部分；Vulkan 传统 graphics pipeline 创建后，shader 阶段通常不能随意替换，除非重新创建 pipeline 或使用相关动态扩展机制。

```cpp
auto stages = Shader::GetInstance().GetStage();

vk::GraphicsPipelineCreateInfo createInfo;
createInfo.setStages(stages);
```

当前项目在 `RenderProcess::InitPipeline()` 中设置 shader：

```cpp
auto stages = Shader::GetInstance().GetStage();
createInfo.setStages(stages);
```

随后同一个 `vk::GraphicsPipelineCreateInfo` 还会设置 vertex input、input assembly、viewport、rasterization、multisampling、color blending、pipeline layout、render pass 等固定功能状态。  
最终创建出的 pipeline 同时包含 shader 程序和固定功能状态。

---

### 1.3.5 绑定 Pipeline 并绘制：`vk::CommandBuffer::bindPipeline`

目标：在命令缓冲中绑定包含 shader 的 graphics pipeline，然后发起 draw call。

```cpp
cmdBuf.bindPipeline(
    vk::PipelineBindPoint::eGraphics,
    renderProcess->pipeline
);

cmdBuf.draw(3, 1, 0, 0);
```

执行到 `draw` 时，GPU 会按 pipeline 中配置的阶段调用 shader。  
当前项目的执行路径是：

1. vertex shader 执行 3 次，生成三角形的 3 个顶点；
2. 图元装配把顶点组成 triangle list；
3. rasterization 把三角形转换成片元；
4. fragment shader 对每个片元输出红色；
5. color attachment 写入 swapchain image；
6. render pass 结束后图像进入 `ePresentSrcKHR`，等待 present。

# 2 Shader 接口

## 2.1 概念

Shader 不是孤立运行的。  
它必须和 pipeline、descriptor、push constant、vertex input、render pass attachment 等接口保持一致。Vulkan 的很多 shader 错误并不是“shader 本身写错”，而是 shader 声明和 C++ 侧 pipeline 配置不匹配。

常见接口包括：

1. vertex input：顶点属性如何进入 vertex shader；
2. stage input/output：不同 shader 阶段之间如何传递数据；
3. descriptor：uniform buffer、storage buffer、sampler、sampled image 等资源如何绑定；
4. push constant：少量频繁更新的数据如何传给 shader；
5. color attachment output：fragment shader 输出如何对应 render pass 的颜色附件。

当前项目还没有 vertex buffer、descriptor set 和 push constant，因此 shader 接口非常简单：

1. vertex shader 使用 `gl_VertexIndex` 内建变量；
2. fragment shader 只声明 `layout(location = 0) out vec4 fragColor`；
3. pipeline 不需要 vertex attribute 描述；
4. pipeline layout 暂时为空。

## 2.2 Vertex Input 接口

### 2.2.1 当前项目：无 Vertex Buffer

目标：理解为什么当前 pipeline 的 vertex input 可以为空。  
项目的 vertex shader 直接根据 `gl_VertexIndex` 生成顶点位置，不从 vertex buffer 读取属性。

```glsl
vec2 position[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    gl_Position = vec4(position[gl_VertexIndex], 0.0, 1.0);
}
```

因此 C++ 侧可以使用空的 `vk::PipelineVertexInputStateCreateInfo`：

```cpp
vk::PipelineVertexInputStateCreateInfo inputState;
createInfo.setPVertexInputState(&inputState);
```

这适合入门阶段验证 pipeline 能否跑通，但真实项目通常会把顶点位置、UV、法线、颜色等数据放入 vertex buffer。

---

### 2.2.2 使用 Vertex Buffer 时：`layout(location = N) in`

目标：理解 shader 输入属性和 pipeline vertex input state 的对应关系。  
如果 vertex shader 改成从顶点属性读取位置和颜色，通常会写成：

```glsl
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragInputColor;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragInputColor = inColor;
}
```

C++ 侧就必须配置 binding 和 attribute：

```cpp
vk::VertexInputBindingDescription binding;
binding.setBinding(0)
       .setStride(sizeof(Vertex))
       .setInputRate(vk::VertexInputRate::eVertex);

std::array<vk::VertexInputAttributeDescription, 2> attributes;
attributes[0].setBinding(0)
             .setLocation(0)
             .setFormat(vk::Format::eR32G32Sfloat)
             .setOffset(offsetof(Vertex, position));

attributes[1].setBinding(0)
             .setLocation(1)
             .setFormat(vk::Format::eR32G32B32Sfloat)
             .setOffset(offsetof(Vertex, color));

vk::PipelineVertexInputStateCreateInfo inputState;
inputState.setVertexBindingDescriptions(binding)
          .setVertexAttributeDescriptions(attributes);
```

这里最关键的是 `location` 对齐：

1. `layout(location = 0) in vec2 inPosition` 对应 attribute location 0；
2. `layout(location = 1) in vec3 inColor` 对应 attribute location 1；
3. `format` 必须和 shader 中变量类型匹配，例如 `vec2` 对应 `eR32G32Sfloat`。

## 2.3 Stage Input / Output 接口

### 2.3.1 Vertex 到 Fragment 的数据传递

目标：通过 `layout(location = N)` 在 shader 阶段之间传递插值数据。  
例如 vertex shader 输出颜色，fragment shader 接收颜色：

```glsl
// vertex shader
layout(location = 0) out vec3 fragInputColor;

void main() {
    fragInputColor = vec3(1.0, 0.0, 0.0);
}
```

```glsl
// fragment shader
layout(location = 0) in vec3 fragInputColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(fragInputColor, 1.0);
}
```

两个 shader 之间的 `location` 必须匹配。  
如果 vertex shader 输出 `location = 0`，fragment shader 却读取 `location = 1`，就会出现接口不匹配问题。

顶点输出到片元输入通常会经过插值。  
例如三角形三个顶点颜色不同，fragment shader 收到的是当前片元位置插值得到的颜色，而不是某个固定顶点的颜色。

---

### 2.3.2 Fragment Output 与 Render Pass Attachment

目标：理解 fragment shader 输出和 framebuffer attachment 的关系。  
当前 fragment shader 写入：

```glsl
layout(location = 0) out vec4 fragColor;
```

render pass 中 subpass 配置了一个 color attachment：

```cpp
vk::AttachmentReference reference;
reference.setAttachment(0)
         .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

vk::SubpassDescription subpass;
subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
       .setColorAttachments(reference);
```

因此 fragment shader 的 `location = 0` 会写入 subpass 的第 0 个 color attachment。  
这个 attachment 在当前项目中最终对应某个 swapchain image view，也就是窗口中显示的颜色图像。

## 2.4 Descriptor 与 Uniform

### 2.4.1 Descriptor 的作用

目标：理解 shader 如何访问 buffer、image、sampler 等外部资源。  
当前项目的 shader 不读取外部资源，所以 pipeline layout 为空：

```cpp
vk::PipelineLayoutCreateInfo createInfo;
layout = Context::GetInstance().device.createPipelineLayout(createInfo);
```

当 shader 需要读取 uniform buffer 或纹理时，就需要 descriptor set layout。  
例如读取一个 uniform buffer：

```glsl
layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;
```

C++ 侧需要创建对应的 descriptor set layout binding：

```cpp
vk::DescriptorSetLayoutBinding cameraBinding;
cameraBinding.setBinding(0)
             .setDescriptorType(vk::DescriptorType::eUniformBuffer)
             .setDescriptorCount(1)
             .setStageFlags(vk::ShaderStageFlagBits::eVertex);
```

这里的对应关系是：

1. `set = 0` 对应第 0 个 descriptor set；
2. `binding = 0` 对应该 set 内的第 0 个 binding；
3. `uniform buffer` 对应 `vk::DescriptorType::eUniformBuffer`；
4. `stageFlags` 表示这个资源在哪些 shader 阶段可见。

---

### 2.4.2 采样纹理：`sampler2D`

目标：理解 image、image view、sampler 如何进入 shader。  
如果 fragment shader 要采样一张纹理，shader 侧通常写成：

```glsl
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedoTexture, fragUV);
}
```

C++ 侧需要准备：

1. `vk::Image`：纹理图像数据；
2. `vk::DeviceMemory`：纹理 image 背后的内存；
3. `vk::ImageView`：shader 访问纹理的视图；
4. `vk::Sampler`：过滤、寻址、mipmap 策略；
5. `vk::DescriptorSet`：把 image view + sampler 绑定到 `set/binding`。

这说明 shader 中的一个 `sampler2D` 并不只是单个 Vulkan 对象，而是 image view、sampler、descriptor 多个对象协作的结果。

## 2.5 Push Constant

### 2.5.1 少量数据快速传递：`layout(push_constant)`

目标：理解 push constant 适合传递小块、频繁变化的数据。  
例如每次 draw 更新一个模型矩阵或颜色，可以用 push constant：

```glsl
layout(push_constant) uniform PushData {
    mat4 model;
    vec4 color;
} pushData;
```

C++ 侧 pipeline layout 需要声明 push constant range：

```cpp
vk::PushConstantRange range;
range.setStageFlags(vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment)
     .setOffset(0)
     .setSize(sizeof(PushData));

vk::PipelineLayoutCreateInfo createInfo;
createInfo.setPushConstantRanges(range);
```

绘制前通过命令缓冲写入：

```cpp
cmdBuf.pushConstants(
    pipelineLayout,
    vk::ShaderStageFlagBits::eVertex |
    vk::ShaderStageFlagBits::eFragment,
    0,
    sizeof(PushData),
    &pushData
);
```

push constant 的优点是调用简单、适合小数据；限制是容量较小，具体上限由物理设备给出。  
大块 per-frame 或 per-object 数据通常仍然使用 uniform buffer / storage buffer。

# 3 Shader Module 与 Pipeline 的关系

## 3.1 概念

`vk::ShaderModule` 和 `vk::Pipeline` 的关系容易混淆。  
`ShaderModule` 是单个 shader 阶段的 SPIR-V 代码容器；`Pipeline` 是完整渲染状态对象，包含 shader stages 和大量固定功能状态。

可以理解为：

```text
ShaderModule
    -> PipelineShaderStageCreateInfo
    -> GraphicsPipelineCreateInfo
    -> Pipeline
```

Pipeline 创建时会读取 shader module 中的代码，并把它和其他状态组合成可执行的 pipeline。  
因此，传统用法下 shader 变了就需要重新创建 pipeline，而不只是替换 shader module。

当前项目的初始化顺序是：

```text
Shader::Init
    -> createShaderModule(vertex)
    -> createShaderModule(fragment)
    -> RenderProcess::InitPipeline
    -> createGraphicsPipeline
    -> Renderer::Render
```

这说明 shader 必须早于 pipeline 创建完成。  
因为 `vk::GraphicsPipelineCreateInfo::setStages` 需要引用已经创建好的 shader modules。

## 3.2 生命周期

Shader module 是 device 级资源，销毁时使用：

```cpp
device.destroyShaderModule(vertexModule);
device.destroyShaderModule(fragmentModule);
```

普通规则是：

1. 创建 shader module；
2. 用 shader module 创建 pipeline；
3. pipeline 创建完成后，如果不再需要用它创建其他 pipeline，可以销毁 shader module；
4. pipeline 自身继续可用。

当前项目为了让结构更直观，`Shader` 单例持续保存 `vertexModule` 和 `fragmentModule`，直到退出：

```cpp
Shader::~Shader() {
    auto& device = Context::GetInstance().device;
    device.destroyShaderModule(vertexModule);
    device.destroyShaderModule(fragmentModule);
}
```

退出顺序中需要保证：

1. `vk::Device` 仍然有效时销毁 shader module；
2. 不要在 device 销毁后再销毁 shader module；
3. 如果 shader module 只用于 pipeline 创建，可以在 pipeline 创建后提前释放，减少长期持有的对象。

## 3.3 实践建议

1. 源码使用 GLSL/HLSL 编写，运行时加载 SPIR-V，不要把 `.vert` / `.frag` 源码直接交给 Vulkan。
2. `.spv` 文件按二进制读取，并确保大小是 `uint32_t` 的整数倍。
3. `vk::ShaderModuleCreateInfo::codeSize` 是字节数，不是 `uint32_t` 数量。
4. `pCode` 必须指向有效的 SPIR-V word 数组。
5. `PipelineShaderStageCreateInfo::setStage` 要和 shader 实际用途一致。
6. `setPName("main")` 必须匹配 shader 入口函数名。
7. vertex shader 输入 location 要和 pipeline vertex attribute location 对齐。
8. shader 阶段之间的 input/output location 要对齐。
9. fragment shader output location 要和 render pass/subpass color attachment 对应。
10. descriptor 的 `set/binding` 必须和 descriptor set layout 保持一致。
11. 修改 shader 后通常需要重新编译 SPIR-V，并重新创建依赖它的 pipeline。
12. 开发阶段保持验证层开启，shader interface mismatch 往往能通过验证层更早发现。
