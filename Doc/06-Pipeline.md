# 1 Graphics Pipeline

## 1.1 概念

`vk::Pipeline`是 Vulkan 中描述 GPU 如何执行渲染或计算工作的核心状态对象。  
在图形渲染里，通常说的 pipeline 指 **Graphics Pipeline**，它把 shader 阶段和大量固定功能状态组合成一个可执行的渲染配置。

可以把 Graphics Pipeline 理解成一条完整的 GPU 渲染流水线：

```text
Vertex Input
    -> Vertex Shader
    -> Input Assembly
    -> Viewport / Scissor
    -> Rasterization
    -> Fragment Shader
    -> Depth / Stencil Test
    -> Color Blending
    -> Color Attachment
```

Vulkan 和 OpenGL 一个很大的区别是：Vulkan 把大量渲染状态提前固化到 pipeline 中。  
在 OpenGL 中，很多状态可以通过全局状态机随时修改；在 Vulkan 中，更多状态需要在创建 pipeline 时一次性声明，绘制时只绑定已经创建好的 pipeline。

这种设计的好处是：

1. 驱动可以在 pipeline 创建阶段完成更多校验和编译；
2. draw call 阶段状态更明确，运行时开销更可控；
3. 多线程准备 pipeline 和命令缓冲更容易；
4. 渲染状态不再依赖隐式全局状态机，更适合大型工程管理。

代价是：pipeline 创建信息非常长，且许多状态变更都可能需要重新创建 pipeline。  
例如 shader、render pass、顶点输入格式、光栅化状态、混合状态等发生变化时，传统 graphics pipeline 往往需要重新创建。

当前项目的 `RenderProcess` 中保存了三个关键对象：

```cpp
vk::Pipeline pipeline;
vk::PipelineLayout layout;
vk::RenderPass renderPass;
```

它们之间的关系是：

1. `vk::RenderPass`：描述渲染目标 attachment 的格式、加载/保存行为、布局转换；
2. `vk::PipelineLayout`：描述 shader 使用哪些 descriptor set 和 push constant；
3. `vk::Pipeline`：把 shader、固定功能状态、pipeline layout、render pass 组合起来。

常见误区：

1. **把 pipeline 当成 shader**：shader 只是 pipeline 的一部分，pipeline 还包含大量固定功能状态。
2. **以为修改 viewport 一定不需要重建 pipeline**：如果没有声明 dynamic state，viewport/scissor 固定在 pipeline 里，窗口尺寸变化时可能需要重建 pipeline。
3. **忘记 pipeline layout**：即使 shader 暂时不使用 descriptor，也仍然需要一个 pipeline layout。
4. **render pass 不匹配**：graphics pipeline 创建时指定的 render pass 必须和实际 begin render pass 时使用的 render pass 兼容。
5. **pipeline 创建后随便改状态**：Vulkan 中大多数 pipeline state 是不可变的，修改状态通常意味着创建另一个 pipeline。

### 1.1.1 Pipeline 与 PSO

在渲染引擎语境里，经常会提到 **PSO（Pipeline State Object）**。  
PSO 不是 Vulkan 独有名词，而是一种更通用的图形 API 设计思路：把绘制所需的大量状态预先组合成一个对象，绘制时直接绑定这个对象，而不是像旧式状态机 API 那样逐条修改全局状态。

从这个角度看，Vulkan 的 `vk::Pipeline` 就是最典型的 PSO 落地形式之一。  
当前 graphics pipeline 中会被打包进来的内容包括：

1. shader stages；
2. vertex input；
3. input assembly；
4. viewport/scissor（如果不是 dynamic state）；
5. rasterization；
6. multisampling；
7. depth/stencil；
8. color blending；
9. render pass 兼容信息；
10. pipeline layout。

因此可以粗略地记成：

```text
Vulkan Graphics Pipeline ≈ 一种具体的 PSO
```

不过更严谨地说，Vulkan 并不是把所有渲染状态都塞进单一对象。  
和 pipeline 紧密协作的还有：

1. `vk::PipelineLayout`：描述外部资源接口；
2. `vk::RenderPass`：描述 attachment 规则与 subpass 兼容关系；
3. dynamic state：把部分状态从 pipeline 中拆出去，延迟到命令缓冲里设置。

所以，PSO 更像一种理解 Vulkan pipeline 的思维框架：  
**预先组合状态、绘制时直接绑定、减少运行时驱动拼装状态的成本。**

## 1.2 创建流程

### 1.2.1 准备 Pipeline Layout：`vk::PipelineLayoutCreateInfo`

目标：声明 shader 访问资源的接口布局。  
Pipeline layout 不描述 shader 代码本身，而是描述 shader 会通过哪些 descriptor set layout 和 push constant range 访问外部资源。

当前项目的 shader 不使用 uniform buffer、texture、sampler、push constant，因此 pipeline layout 可以为空：

```cpp
vk::PipelineLayoutCreateInfo createInfo;
layout = Context::GetInstance()
    .device
    .createPipelineLayout(createInfo);
```

如果后续加入 uniform buffer，例如相机矩阵：

```glsl
layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;
```

C++ 侧就需要创建 descriptor set layout，并写入 pipeline layout：

```cpp
vk::PipelineLayoutCreateInfo createInfo;
createInfo.setSetLayouts(cameraDescriptorSetLayout);

vk::PipelineLayout layout =
    device.createPipelineLayout(createInfo);
```

如果 shader 使用 push constant，也需要在 pipeline layout 中声明：

```cpp
vk::PushConstantRange range;
range.setStageFlags(vk::ShaderStageFlagBits::eVertex)
     .setOffset(0)
     .setSize(sizeof(PushData));

vk::PipelineLayoutCreateInfo createInfo;
createInfo.setPushConstantRanges(range);
```

因此，pipeline layout 是 CPU 侧资源绑定规则与 shader 侧 `set/binding/push_constant` 声明之间的桥梁。

---

### 1.2.2 准备 Shader Stages：`vk::PipelineShaderStageCreateInfo`

目标：把已经创建好的 shader modules 指定到 graphics pipeline 的不同阶段。  
当前项目中，`Shader::GetInstance().GetStage()` 返回 vertex shader 和 fragment shader 两个阶段。

```cpp
auto stages = Shader::GetInstance().GetStage();

vk::GraphicsPipelineCreateInfo createInfo;
createInfo.setStages(stages);
```

stage 信息本质上包含三件事：

```cpp
stage.setStage(vk::ShaderStageFlagBits::eVertex)
     .setModule(vertexModule)
     .setPName("main");
```

1. `setStage`：指定这是 vertex、fragment、geometry、compute 等哪个阶段；
2. `setModule`：指定该阶段使用哪个 `vk::ShaderModule`；
3. `setPName`：指定 shader 入口函数名。

Graphics pipeline 至少需要 vertex shader。  
如果要把结果写入 color attachment，通常还需要 fragment shader。当前项目就是最基础的 vertex + fragment 组合。

---

### 1.2.3 配置 Vertex Input：`vk::PipelineVertexInputStateCreateInfo`

目标：描述 vertex buffer 中的数据如何映射到 vertex shader 的 `layout(location = N) in` 输入变量。  
当前项目没有 vertex buffer，顶点位置直接写在 shader 中，并通过 `gl_VertexIndex` 取值，因此 vertex input 可以为空。

```cpp
vk::PipelineVertexInputStateCreateInfo inputState;
createInfo.setPVertexInputState(&inputState);
```

当前 vertex shader：

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

因为 shader 没有声明：

```glsl
layout(location = 0) in vec2 inPosition;
```

所以 C++ 侧也不需要填写 `vk::VertexInputBindingDescription` 和 `vk::VertexInputAttributeDescription`。

如果后续加入 vertex buffer，通常需要：

```cpp
vk::VertexInputBindingDescription binding;
binding.setBinding(0)
       .setStride(sizeof(Vertex))
       .setInputRate(vk::VertexInputRate::eVertex);

vk::VertexInputAttributeDescription positionAttr;
positionAttr.setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(offsetof(Vertex, position));

vk::PipelineVertexInputStateCreateInfo inputState;
inputState.setVertexBindingDescriptions(binding)
          .setVertexAttributeDescriptions(positionAttr);
```

这里的 `location` 必须和 shader 中的 `layout(location = 0)` 对齐。

---

### 1.2.4 配置输入装配：`vk::PipelineInputAssemblyStateCreateInfo`

目标：描述顶点如何组成图元。  
当前项目使用三个顶点画一个三角形，因此拓扑类型是 `vk::PrimitiveTopology::eTriangleList`。

```cpp
vk::PipelineInputAssemblyStateCreateInfo inputAsm;
inputAsm.setPrimitiveRestartEnable(false)
        .setTopology(vk::PrimitiveTopology::eTriangleList);

createInfo.setPInputAssemblyState(&inputAsm);
```

常见 topology：

1. `ePointList`：每个顶点是一个点；
2. `eLineList`：每两个顶点组成一条线；
3. `eLineStrip`：连续顶点组成折线；
4. `eTriangleList`：每三个顶点组成一个三角形；
5. `eTriangleStrip`：连续顶点组成三角形带。

`primitiveRestartEnable` 通常用于 strip 类拓扑，例如 triangle strip。  
当前使用 triangle list，不需要 primitive restart。

---

### 1.2.5 配置 Viewport 与 Scissor：`vk::PipelineViewportStateCreateInfo`

目标：定义裁剪空间坐标如何映射到 framebuffer，以及哪些区域允许写入。  
Viewport 负责坐标变换，scissor 负责裁剪写入区域。

当前项目使用窗口尺寸作为 viewport 和 scissor：

```cpp
vk::PipelineViewportStateCreateInfo viewportState;

vk::Viewport viewport(0, 0, width, height, 0, 1);
viewportState.setViewports(viewport);

vk::Rect2D rect(
    {0, 0},
    {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}
);
viewportState.setScissors(rect);

createInfo.setPViewportState(&viewportState);
```

`vk::Viewport` 的关键字段：

1. `x / y`：viewport 左上角或起点；
2. `width / height`：viewport 尺寸；
3. `minDepth / maxDepth`：深度范围，通常是 0 到 1。

`vk::Rect2D` 定义 scissor：

1. `offset`：裁剪区域起点；
2. `extent`：裁剪区域尺寸。

当前项目没有设置 dynamic viewport/scissor，因此 viewport 和 scissor 固化在 pipeline 中。  
如果窗口 resize 后 swapchain extent 改变，pipeline 中的 viewport/scissor 也需要同步更新，通常意味着重建 pipeline，或者改用 dynamic state。

---

### 1.2.6 配置光栅化：`vk::PipelineRasterizationStateCreateInfo`

目标：描述图元如何被转换成片元。  
Rasterization 阶段会把三角形、线、点等图元转换成 framebuffer 上的 fragment。

当前项目配置：

```cpp
vk::PipelineRasterizationStateCreateInfo rastInfo;
rastInfo.setRasterizerDiscardEnable(false)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f);

createInfo.setPRasterizationState(&rastInfo);
```

关键参数：

1. `setRasterizerDiscardEnable(false)`：不丢弃光栅化结果，继续生成 fragment；
2. `setCullMode(eBack)`：剔除背面三角形；
3. `setFrontFace(eCounterClockwise)`：逆时针顶点顺序被认为是正面；
4. `setPolygonMode(eFill)`：以填充模式绘制多边形；
5. `setLineWidth(1.0f)`：线宽，普通渲染通常为 1。

`frontFace` 和顶点顺序关系非常重要。  
如果三角形顶点顺序与 `frontFace` 设置相反，并且开启了 back-face culling，就可能导致三角形被剔除，看不到任何画面。

如果想临时排查是否是剔除问题，可以把 cull mode 设为：

```cpp
rastInfo.setCullMode(vk::CullModeFlagBits::eNone);
```

---

### 1.2.7 配置多重采样：`vk::PipelineMultisampleStateCreateInfo`

目标：配置 MSAA 等多重采样行为。  
当前项目不启用 MSAA，因此采样数为 `e1`。

```cpp
vk::PipelineMultisampleStateCreateInfo multisample;
multisample.setSampleShadingEnable(false)
           .setRasterizationSamples(vk::SampleCountFlagBits::e1);

createInfo.setPMultisampleState(&multisample);
```

`setRasterizationSamples(e1)` 表示每个像素只有一个样本，不进行多重采样抗锯齿。  
如果后续开启 MSAA，需要同时处理：

1. physical device 支持的 sample count；
2. color/depth attachment 的 sample count；
3. render pass attachment 描述；
4. resolve attachment；
5. pipeline multisample state。

MSAA 不是只改 pipeline 一个字段就能完成的功能，它涉及 image、render pass、framebuffer 和 pipeline 多个对象。

---

### 1.2.8 配置颜色混合：`vk::PipelineColorBlendStateCreateInfo`

目标：描述 fragment shader 输出颜色如何写入 color attachment。  
当前项目不启用 alpha blending，只允许写入 RGBA 四个通道。

```cpp
vk::PipelineColorBlendAttachmentState attachment;
attachment.setBlendEnable(false)
          .setColorWriteMask(
              vk::ColorComponentFlagBits::eR |
              vk::ColorComponentFlagBits::eG |
              vk::ColorComponentFlagBits::eB |
              vk::ColorComponentFlagBits::eA
          );

vk::PipelineColorBlendStateCreateInfo blend;
blend.setLogicOpEnable(false)
     .setAttachments(attachment);

createInfo.setPColorBlendState(&blend);
```

`colorWriteMask` 决定哪些通道可以被写入。  
如果只允许写 RGB，不写 alpha，可以去掉 `eA`。

如果要启用普通透明混合，通常会设置：

```cpp
attachment.setBlendEnable(true)
          .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
          .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
          .setColorBlendOp(vk::BlendOp::eAdd)
          .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
          .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
          .setAlphaBlendOp(vk::BlendOp::eAdd);
```

混合发生在 fragment shader 输出之后、写入 color attachment 之前。  
它依赖 render pass 中的 color attachment，也依赖 framebuffer 绑定的实际 image view。

---

### 1.2.9 配置深度与模板：`vk::PipelineDepthStencilStateCreateInfo`

目标：控制 depth test、depth write、stencil test。  
当前项目还没有 depth image，也没有在 render pass 中声明 depth attachment，因此 pipeline 暂时没有设置 depth stencil state。

如果后续加入 3D 渲染，通常需要：

```cpp
vk::PipelineDepthStencilStateCreateInfo depthStencil;
depthStencil.setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthCompareOp(vk::CompareOp::eLess)
            .setDepthBoundsTestEnable(false)
            .setStencilTestEnable(false);

createInfo.setPDepthStencilState(&depthStencil);
```

同时还必须创建 depth image、depth image view，并在 render pass/framebuffer 中加入 depth attachment。  
如果只在 pipeline 中启用 depth test，却没有对应 depth attachment，pipeline 和 render pass 将不匹配。

---

### 1.2.10 指定 Render Pass 与 Pipeline Layout：`setRenderPass` / `setLayout`

目标：把 pipeline 绑定到兼容的 render pass 和资源接口布局上。  
当前项目在创建 graphics pipeline 时写入：

```cpp
createInfo.setRenderPass(renderPass)
          .setLayout(layout);
```

`setRenderPass(renderPass)` 表示这个 pipeline 会在与该 render pass 兼容的 render pass 实例中使用。  
当前 render pass 的 color attachment format 来自 swapchain：

```cpp
attachDesc.setFormat(Context::GetInstance().swapchain->info.format.format)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
```

因此 swapchain format 变化时，render pass 可能需要重建，依赖该 render pass 的 pipeline 也可能需要重建。

`setLayout(layout)` 则绑定 pipeline layout。  
后续命令缓冲中绑定 descriptor set 或 push constant 时，也需要使用与 pipeline 兼容的 layout。

---

### 1.2.11 创建 Graphics Pipeline：`vk::Device::createGraphicsPipeline`

目标：把所有 shader state 和 fixed-function state 组合成真正可绑定的 pipeline 对象。

```cpp
auto result = Context::GetInstance()
    .device
    .createGraphicsPipeline(nullptr, createInfo);

if (result.result != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to create pipeline");
}

pipeline = result.value;
```

第一个参数是 pipeline cache。当前项目传入 `nullptr`，表示不使用 pipeline cache。  
大型项目中可以使用 `vk::PipelineCache` 缓存 pipeline 编译结果，降低后续启动或创建 pipeline 的成本。

Pipeline 创建失败常见原因：

1. shader module 无效或入口名错误；
2. shader 接口和 pipeline state 不匹配；
3. render pass attachment 与 fragment output 不匹配；
4. viewport/scissor 数量缺失；
5. depth stencil state 与 render pass 不兼容；
6. 某些特性未启用却使用了对应状态。

# 2 Pipeline 使用

## 2.1 概念

创建 pipeline 只是准备渲染状态，真正使用发生在命令缓冲录制阶段。  
在 Vulkan 中，draw call 本身不会携带完整渲染状态，而是依赖当前命令缓冲绑定的 pipeline、descriptor set、vertex buffer、index buffer 等状态。

当前项目每帧渲染的大致顺序是：

```text
acquireNextImageKHR
    -> begin command buffer
    -> begin render pass
    -> bind graphics pipeline
    -> draw
    -> end render pass
    -> submit
    -> present
```

Pipeline 在这个流程中负责决定：

1. 使用哪组 shader；
2. 顶点如何组成图元；
3. 图元如何被光栅化；
4. fragment 输出如何写入 color attachment；
5. 是否进行深度/模板测试；
6. 是否进行颜色混合。

## 2.2 命令缓冲绑定流程

### 2.2.1 开始 Render Pass：`vk::CommandBuffer::beginRenderPass`

目标：进入与 pipeline 兼容的 render pass 实例。  
Graphics pipeline 必须在 render pass 内绑定和绘制，且该 render pass 要与创建 pipeline 时指定的 render pass 兼容。

```cpp
vk::RenderPassBeginInfo renderPassBegin;
renderPassBegin.setRenderPass(renderProcess->renderPass)
               .setRenderArea(area)
               .setFramebuffer(swapchain->framebuffers[imageIndex])
               .setClearValues(clearValue);

cmdBuf.beginRenderPass(renderPassBegin, {});
```

这里的 framebuffer 对应当前 acquired swapchain image。  
pipeline 的 fragment output 最终会写入这个 framebuffer 中的 color attachment。

---

### 2.2.2 绑定 Graphics Pipeline：`vk::CommandBuffer::bindPipeline`

目标：把后续 draw call 使用的 graphics pipeline 设置到命令缓冲状态中。

```cpp
cmdBuf.bindPipeline(
    vk::PipelineBindPoint::eGraphics,
    renderProcess->pipeline
);
```

`vk::PipelineBindPoint::eGraphics` 表示绑定的是 graphics pipeline。  
如果是 compute pipeline，则使用：

```cpp
vk::PipelineBindPoint::eCompute
```

绑定 pipeline 后，后续 `draw` 会按照该 pipeline 中的 shader 和固定功能状态执行。  
如果需要绘制不同材质、不同 shader、不同混合状态的对象，通常需要在命令缓冲中切换不同 pipeline。

---

### 2.2.3 发起绘制：`vk::CommandBuffer::draw`

目标：触发 graphics pipeline 执行。  
当前项目绘制一个三角形：

```cpp
cmdBuf.draw(3, 1, 0, 0);
```

参数含义：

1. `vertexCount = 3`：绘制 3 个顶点；
2. `instanceCount = 1`：绘制 1 个实例；
3. `firstVertex = 0`：顶点索引从 0 开始；
4. `firstInstance = 0`：实例索引从 0 开始。

由于项目没有绑定 vertex buffer，vertex shader 依赖 `gl_VertexIndex` 生成位置。  
如果后续使用 vertex buffer，draw 前还需要：

```cpp
cmdBuf.bindVertexBuffers(0, vertexBuffer, offsets);
```

如果使用 index buffer，则使用：

```cpp
cmdBuf.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
cmdBuf.drawIndexed(indexCount, 1, 0, 0, 0);
```

## 2.3 Dynamic State

### 2.3.1 为什么需要 Dynamic State

目标：减少因为少量状态变化导致的 pipeline 重建。  
默认情况下，viewport、scissor、line width、blend constants、depth bias 等状态可以固化在 pipeline 中。  
如果把它们声明为 dynamic state，就可以在命令缓冲中动态设置。

例如把 viewport 和 scissor 改成动态状态：

```cpp
std::array dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
};

vk::PipelineDynamicStateCreateInfo dynamicState;
dynamicState.setDynamicStates(dynamicStates);

createInfo.setPDynamicState(&dynamicState);
```

此时 pipeline 创建时仍然需要声明 viewport/scissor 数量，但具体值可以在命令缓冲中设置：

```cpp
cmdBuf.setViewport(0, viewport);
cmdBuf.setScissor(0, scissor);
```

对窗口渲染来说，dynamic viewport/scissor 很常用。  
因为 swapchain resize 后，如果只有尺寸变化，动态设置 viewport/scissor 可以减少因尺寸变化导致的 pipeline 重建需求。

# 3 Pipeline 与其他对象的关系

## 3.1 Pipeline 与 Shader

Pipeline 包含 shader stages，但 shader module 不是 pipeline 的全部。  
当前项目中 shader stage 来自：

```cpp
auto stages = Shader::GetInstance().GetStage();
createInfo.setStages(stages);
```

这表示 pipeline 创建时会使用 `Shader` 中保存的 vertex/fragment shader modules。  
pipeline 创建完成后，draw call 只需要绑定 pipeline，不需要再次单独绑定 shader module。

如果 shader 代码发生变化，需要重新编译 SPIR-V，并重新创建 shader module 和依赖它的 pipeline。

## 3.2 Pipeline 与 Render Pass

Graphics pipeline 创建时需要指定 render pass：

```cpp
createInfo.setRenderPass(renderPass);
```

这意味着 pipeline 与 render pass 的 attachment 格式、subpass 结构存在兼容关系。  
当前项目的 render pass 使用 swapchain format 作为 color attachment format，因此 swapchain format 改变时，render pass 和 pipeline 都需要重新考虑。

当前项目初始化顺序是：

```text
InitSwapchain
    -> InitRenderPass
    -> InitLayout
    -> CreateFramebuffers
    -> InitPipeline
```

这个顺序反映了依赖关系：

1. render pass 需要 swapchain format；
2. framebuffer 需要 render pass 和 swapchain image views；
3. pipeline 需要 render pass 和 pipeline layout；
4. command buffer 录制时需要同时使用 framebuffer 和 pipeline。

## 3.3 Pipeline 与 Pipeline Layout

Pipeline layout 描述 descriptor set 和 push constant 的接口规则。  
当前项目的 layout 为空，但仍然是 pipeline 创建的必需输入：

```cpp
createInfo.setLayout(layout);
```

当 shader 增加如下资源：

```glsl
layout(set = 0, binding = 0) uniform sampler2D tex;
```

pipeline layout 就必须包含匹配的 descriptor set layout。  
否则 pipeline 创建或绘制时会出现资源接口不匹配问题。

可以把 pipeline layout 理解为 shader 外部资源接口的“函数签名”。  
shader 访问哪些资源，pipeline layout 就必须声明哪些资源布局。

## 3.4 Pipeline 与 Swapchain 重建

窗口 resize 时，swapchain extent 可能变化。  
在当前项目中，viewport 和 scissor 是固定写入 pipeline 的：

```cpp
vk::Viewport viewport(0, 0, width, height, 0, 1);
viewportState.setViewports(viewport);
```

因此如果窗口尺寸变化，至少需要重新考虑：

1. swapchain；
2. swapchain image views；
3. framebuffers；
4. render pass；
5. pipeline viewport/scissor；
6. 可能依赖 extent 的 projection 参数。

如果后续改用 dynamic viewport/scissor，尺寸变化时仍然要重建 swapchain 和 framebuffer，但 pipeline 本身可能不必因为 viewport/scissor 数值变化而重建。

## 3.5 生命周期与销毁顺序

Pipeline、pipeline layout、render pass 都是 device 级对象。  
销毁时必须保证 device 仍然有效，并且 GPU 不再使用它们。

推荐退出前先等待 device 空闲：

```cpp
device.waitIdle();
```

然后按依赖关系反向销毁：

```cpp
device.destroyPipeline(pipeline);
device.destroyPipelineLayout(layout);
device.destroyRenderPass(renderPass);
```

当前项目的 `toy2d::Quit()` 中先调用了：

```cpp
ContextInstance.device.waitIdle();
```

这可以避免 GPU 仍在执行使用旧 pipeline 的命令。  
在实际资源销毁顺序上，推荐先销毁 pipeline，再销毁 pipeline layout 和 render pass。这样更符合“先销毁使用者，再销毁被依赖对象”的通用规则。

## 3.6 Pipeline 与 PSO Cache / `vk::PipelineCache`

讨论 Vulkan pipeline 时，经常还会听到 **PSO cache**。  
这里要区分三个层次：

1. **PSO / Pipeline**：真正参与绘制绑定的状态对象；
2. **PSO cache**：引擎层面对“哪些状态组合已经创建过”做的缓存；
3. **`vk::PipelineCache`**：Vulkan API 提供的、偏驱动编译结果复用的缓存对象。

三者不是同一个东西，但目标相关：

```text
Pipeline / PSO
    -> 真正拿来 bind 和 draw 的对象

Engine-level PSO cache
    -> 避免重复创建相同配置的 Pipeline

vk::PipelineCache
    -> 帮助驱动复用 pipeline 编译/优化结果
```

### 3.6.1 Pipeline 不是 Cache

`vk::Pipeline` 本身就是可执行状态对象。  
命令缓冲里绑定的是它：

```cpp
cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
```

也就是说，pipeline 是“成品”，不是缓存。  
它已经代表了一组确定的渲染状态和 shader 组合。

### 3.6.2 什么是引擎里的 PSO Cache

在引擎设计里，PSO cache 通常指：

1. 以 shader、blend state、depth state、rasterization、render pass、vertex layout 等为 key；
2. 查表判断这种状态组合是否已经创建过 pipeline；
3. 如果创建过就直接复用；
4. 没创建过再调用 Vulkan 去创建新的 `vk::Pipeline`。

它解决的是：

```text
不要为同一组状态重复创建很多份相同的 pipeline
```

例如概念上会有这样的缓存逻辑：

```cpp
struct PipelineKey {
    ShaderKey shader;
    BlendKey blend;
    RasterKey raster;
    DepthKey depth;
    RenderPassKey renderPass;
};

std::unordered_map<PipelineKey, vk::Pipeline> pipelineCacheMap;
```

这里的 `pipelineCacheMap` 是引擎层面的 PSO cache。  
它缓存的是“已经创建好的 pipeline 对象”。

### 3.6.3 什么是 `vk::PipelineCache`

`vk::PipelineCache` 是 Vulkan 提供的专门对象，用来帮助驱动复用 pipeline 创建时产生的一些内部结果，例如编译、优化、中间数据等。  
它不是 `vk::Pipeline` 的替代品，也不会直接出现在 `bindPipeline` 里。

Vulkan API 用法上，它通常作为创建 pipeline 的辅助输入：

```cpp
vk::PipelineCache pipelineCache = device.createPipelineCache({});

auto result = device.createGraphicsPipeline(
    pipelineCache,
    createInfo
);
```

当前项目中：

```cpp
auto result = Context::GetInstance()
    .device
    .createGraphicsPipeline(nullptr, createInfo);
```

这里传入的是 `nullptr`，表示没有使用 `vk::PipelineCache`。

### 3.6.4 引擎 PSO Cache 与 `vk::PipelineCache` 的关系

二者经常一起出现，但职责不同：

1. **引擎 PSO cache** 负责“同一份 pipeline 不要重复创建”；
2. **`vk::PipelineCache`** 负责“即使要创建新的 pipeline，也尽量复用驱动端已有编译结果”。

可以把它们理解成两层缓存：

```text
第一层：引擎层
    key -> vk::Pipeline

第二层：驱动层
    vk::PipelineCache -> 帮助更快创建 vk::Pipeline
```

因此，“PSO cache” 这个词在工程交流里有时会泛指这两类缓存机制，但严格区分时：

1. 引擎缓存的是 **pipeline 对象本身**；
2. Vulkan `PipelineCache` 缓存的是 **创建 pipeline 时可复用的内部数据**。

### 3.6.5 为什么 PSO Cache 很重要

Vulkan 把很多状态固化进 pipeline 后，draw 阶段 CPU 压力降低了，但代价是 pipeline 创建更重。  
如果项目中 shader 变体、材质变体、render pass 组合很多，pipeline 数量会迅速膨胀。

这时如果没有缓存，问题会很明显：

1. 相同状态反复创建 pipeline，浪费 CPU；
2. 首次进入某些场景时会有明显卡顿；
3. pipeline 创建线程和渲染线程容易互相影响。

所以实际工程里通常会同时做两件事：

1. 用引擎层 PSO cache 避免重复创建同一 pipeline；
2. 用 `vk::PipelineCache` 降低新 pipeline 的创建成本；
3. 更进一步还会做离线预热、启动时预编译、运行时异步创建等优化。

### 3.6.6 当前项目中的位置

当前项目只有一个非常简单的 graphics pipeline，因此还没有引入专门的 PSO cache。  
当前逻辑更接近：

```text
启动时创建一次 pipeline
    -> 渲染循环里反复 bind 同一个 pipeline
```

这对入门项目完全足够。  
当后续加入：

1. 多个 shader；
2. 透明与不透明材质；
3. 不同 blend/depth/raster 状态；
4. 多种 render pass；
5. 更多顶点格式；

就会自然进入“需要 PSO cache”的阶段。  
那时可以先做引擎层 `PipelineKey -> vk::Pipeline` 缓存，再考虑引入 `vk::PipelineCache` 提升 pipeline 创建效率。

# 4 Pipeline 实践建议

## 4.1 调试与设计建议

1. 先用最小 pipeline 跑通三角形：vertex shader、fragment shader、triangle list、无 depth、无 blend。
2. 每加入一个状态都确认对应依赖对象是否已经存在，例如 depth state 需要 depth attachment。
3. shader 改动后重新编译 SPIR-V，并重新创建 pipeline。
4. swapchain format 改变时，重新检查 render pass 和 pipeline 兼容性。
5. viewport/scissor 固化在 pipeline 中时，窗口 resize 可能需要重建 pipeline。
6. 入门阶段可以不使用 pipeline cache；pipeline 数量增多后再考虑 `vk::PipelineCache`。
7. 材质系统中通常会按 shader、render pass、blend/depth/rasterization 状态组织多个 pipeline。
8. pipeline layout 应该随着 descriptor set layout / push constant 的设计一起规划。
9. 如果屏幕一片空白，优先检查 cull mode、front face、render pass attachment、viewport/scissor、shader output。
10. 资源销毁前确保 GPU 已经停止使用相关 pipeline，最简单方式是退出阶段调用 `device.waitIdle()`。

## 4.2 当前项目的 Pipeline 链路

Toy2D 当前 pipeline 链路可以概括为：

```text
Shader::Init
    -> RenderProcess::InitRenderPass
    -> RenderProcess::InitLayout
    -> Swapchain::CreateFramebuffers
    -> RenderProcess::InitPipeline
    -> Renderer::Render
    -> bindPipeline
    -> draw
```

其中 `RenderProcess::InitPipeline` 负责组合：

1. 空 vertex input；
2. triangle list 输入装配；
3. vertex + fragment shader stages；
4. 固定 viewport/scissor；
5. back-face culling；
6. 无 MSAA；
7. 无 alpha blending；
8. 当前 render pass；
9. 当前 pipeline layout。

这条链路是 Vulkan 入门阶段最典型的 graphics pipeline。  
后续扩展 vertex buffer、纹理、uniform、depth test、MSAA、透明混合时，基本都会围绕这个 pipeline 创建流程继续增加状态。
