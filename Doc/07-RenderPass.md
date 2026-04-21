# 1 RenderPass

## 1.1 概念

`vk::RenderPass`描述一次渲染过程中，渲染目标 attachment 如何被使用。  
它不保存图像数据，也不代表具体某一张 framebuffer 图像；它更像是一份“渲染目标使用规则说明书”。

RenderPass 主要回答几个问题：

1. 本次渲染会用到哪些 attachment；
2. 每个 attachment 的格式是什么；
3. 每个 attachment 在进入 render pass 时如何处理旧内容；
4. 每个 attachment 在离开 render pass 时是否保存结果；
5. attachment 在不同阶段应该处于什么 image layout；
6. subpass 如何引用这些 attachment；
7. subpass 之间、render pass 外部与内部之间如何同步。

在当前项目中，render pass 只有一个 color attachment，也就是 swapchain image。  
每帧渲染时，应用从 swapchain 取得一张 image，把它通过 framebuffer 绑定到 render pass 的第 0 个 attachment 上，然后 fragment shader 的输出写入这张 swapchain image。

可以把当前项目中的关系理解为：

```text
Swapchain Image
    -> ImageView
    -> Framebuffer Attachment
    -> RenderPass Attachment Description
    -> Subpass Color Attachment
    -> Fragment Shader Output
```

RenderPass 和 Framebuffer 的区别非常重要：

1. `RenderPass` 描述 attachment 的格式、加载/保存行为、布局转换、subpass 使用方式；
2. `Framebuffer` 绑定具体的 image views，告诉 render pass 本次渲染实际写哪几张图像；
3. 同一个 render pass 可以配合多个 framebuffer 使用，只要 framebuffer 的 attachment 与 render pass 描述兼容。

当前项目中，每个 swapchain image view 都对应一个 framebuffer，但它们共用同一个 render pass。

常见误区：

1. **把 RenderPass 当成实际图像**：RenderPass 不拥有 image，也不拥有 image view。
2. **把 Framebuffer 和 RenderPass 混为一谈**：Framebuffer 是具体资源绑定，RenderPass 是使用规则。
3. **忽略 attachment format**：pipeline、render pass、swapchain image format 必须兼容。
4. **忽略 image layout**：render pass 可以帮 attachment 做布局转换，但前提是 initial/final layout 和 attachment reference layout 写对。
5. **修改 swapchain 后忘记重建 render pass/framebuffer**：如果 swapchain format 或 attachment 配置变化，render pass 和 framebuffer 都可能需要重建。

## 1.2 创建流程

### 1.2.1 描述 Attachment：`vk::AttachmentDescription`

目标：声明 render pass 中会用到哪些 attachment，以及它们的格式、采样数、加载/保存行为和布局转换规则。  
当前项目只有一个 color attachment，对应 swapchain image。

```cpp
vk::AttachmentDescription attachDesc;
attachDesc.setFormat(Context::GetInstance().swapchain->info.format.format)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setLoadOp(vk::AttachmentLoadOp::eClear)
          .setStoreOp(vk::AttachmentStoreOp::eStore)
          .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
          .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
```

关键参数说明：

1. `setFormat`：attachment 的图像格式。当前使用 swapchain format，必须和 swapchain image view 的格式匹配；
2. `setSamples`：采样数。当前没有开启 MSAA，所以是 `e1`；
3. `setLoadOp`：进入 render pass 时如何处理 attachment 旧内容；
4. `setStoreOp`：离开 render pass 时是否保存 attachment 新内容；
5. `setStencilLoadOp / setStencilStoreOp`：模板部分的加载/保存行为；
6. `setInitialLayout`：render pass 开始前 image 期望的布局；
7. `setFinalLayout`：render pass 结束后 image 转换到的布局。

当前项目使用：

```cpp
.setLoadOp(vk::AttachmentLoadOp::eClear)
.setStoreOp(vk::AttachmentStoreOp::eStore)
```

含义是：每帧开始渲染时先清空颜色附件，渲染结束后保留结果。  
因为 swapchain image 最终要 present 到窗口，所以必须 `store`，否则渲染结果可能不会被保存到可呈现图像中。

---

### 1.2.2 选择 LoadOp：`vk::AttachmentLoadOp`

目标：明确 render pass 开始时 attachment 旧内容如何处理。

常见选项：

1. `vk::AttachmentLoadOp::eLoad`：保留旧内容，适合需要在旧图像基础上继续绘制的场景；
2. `vk::AttachmentLoadOp::eClear`：开始时清空 attachment，适合每帧重新绘制整个画面；
3. `vk::AttachmentLoadOp::eDontCare`：不关心旧内容，驱动可以不加载旧数据。

当前项目每帧都清屏后画三角形，因此使用：

```cpp
attachDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
```

清屏颜色不是在 `AttachmentDescription` 里写的，而是在 `vk::RenderPassBeginInfo` 里提供：

```cpp
vk::ClearValue clearValue;
clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);

renderPassBegin.setClearValues(clearValue);
```

如果 `loadOp` 不是 `eClear`，对应 clear value 不会作为该 attachment 的清除值使用。

---

### 1.2.3 选择 StoreOp：`vk::AttachmentStoreOp`

目标：明确 render pass 结束后 attachment 内容是否需要保存。  
对 swapchain color attachment 来说，最终要显示到屏幕，所以必须保存渲染结果。

```cpp
attachDesc.setStoreOp(vk::AttachmentStoreOp::eStore);
```

常见选项：

1. `vk::AttachmentStoreOp::eStore`：保存结果，后续还要读取、present 或继续使用；
2. `vk::AttachmentStoreOp::eDontCare`：不关心结果，适合临时 attachment。

例如某些 MSAA color attachment 只用于中间多采样渲染，最后通过 resolve attachment 输出到 swapchain image，那么 MSAA attachment 自身可能可以使用 `eDontCare`，而 resolve 后的 swapchain attachment 要使用 `eStore`。

---

### 1.2.4 设置 Attachment Layout：`initialLayout` / `finalLayout`

目标：让 render pass 帮 attachment 完成必要的 image layout transition。  
当前项目中，swapchain image 在进入 render pass 时不需要保留旧内容，结束后要进入 present 状态。

```cpp
attachDesc.setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
```

`eUndefined` 表示进入 render pass 前不关心旧内容。  
这和 `loadOp = eClear` 是匹配的：既然每帧都要清屏，旧图像内容没有保留价值。

`ePresentSrcKHR` 表示 render pass 结束后，这张 swapchain image 会被用于 present：

```cpp
presentQueue.presentKHR(presentInfo);
```

如果是普通离屏颜色图，final layout 可能是：

```cpp
vk::ImageLayout::eShaderReadOnlyOptimal
```

表示之后要作为纹理被 shader 采样。  
如果是深度图，常见 layout 则是：

```cpp
vk::ImageLayout::eDepthStencilAttachmentOptimal
```

layout 的选择必须和 attachment 后续用途一致。

---

### 1.2.5 创建 Attachment Reference：`vk::AttachmentReference`

目标：在 subpass 中引用前面声明的 attachment。  
`AttachmentDescription` 只是声明“有一个 attachment”；`AttachmentReference` 才说明 subpass 会如何使用它。

```cpp
vk::AttachmentReference reference;
reference.setAttachment(0)
         .setLayout(vk::ImageLayout::eColorAttachmentOptimal);
```

关键参数：

1. `setAttachment(0)`：引用 render pass attachment 列表中的第 0 个 attachment；
2. `setLayout(eColorAttachmentOptimal)`：在该 subpass 内，这个 attachment 作为 color attachment 使用。

注意这里的 layout 不是 final layout。  
它表示 image 在 subpass 执行期间应该处于什么布局。当前流程是：

```text
initialLayout: eUndefined
    -> subpass layout: eColorAttachmentOptimal
    -> finalLayout: ePresentSrcKHR
```

也就是说，render pass 会在合适位置处理从 undefined 到 color attachment，再从 color attachment 到 present 的布局转换。

---

### 1.2.6 创建 Subpass：`vk::SubpassDescription`

目标：描述一次 render pass 内部的一个渲染子阶段。  
当前项目只有一个 subpass，用于 graphics pipeline，把 fragment shader 输出写入 color attachment。

```cpp
vk::SubpassDescription subpass;
subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
       .setColorAttachments(reference);
```

`setPipelineBindPoint(eGraphics)` 表示这个 subpass 用于 graphics pipeline。  
如果是 compute 工作，则不通过 graphics render pass subpass 组织。

`setColorAttachments(reference)` 表示 fragment shader 的 color output 会写入这个 color attachment。  
当前 fragment shader 中：

```glsl
layout(location = 0) out vec4 fragColor;
```

它会写入 subpass 的第 0 个 color attachment，也就是 `reference` 指向的 attachment 0。

如果使用多个 color attachments，例如 G-Buffer，subpass 可以设置多个 attachment reference：

```cpp
std::array colorRefs = {
    albedoRef,
    normalRef,
    materialRef
};

subpass.setColorAttachments(colorRefs);
```

此时 fragment shader 也需要多个输出：

```glsl
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
```

---

### 1.2.7 设置 Subpass Dependency：`vk::SubpassDependency`

目标：描述 render pass 外部与 subpass 之间，或 subpass 与 subpass 之间的执行和内存依赖。  
当前项目需要保证 color attachment 写入发生在正确的 pipeline 阶段，并且写入权限可用。

```cpp
vk::SubpassDependency dependency;
dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
          .setDstSubpass(0)
          .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
          .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
          .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
```

字段说明：

1. `setSrcSubpass(VK_SUBPASS_EXTERNAL)`：依赖来源是 render pass 外部；
2. `setDstSubpass(0)`：依赖目标是第 0 个 subpass；
3. `setSrcStageMask`：来源阶段；
4. `setDstStageMask`：目标阶段；
5. `setDstAccessMask`：目标阶段需要的访问权限。

当前场景中，color attachment 会在 `eColorAttachmentOutput` 阶段写入，所以目标 stage 和 access mask 分别是：

```cpp
vk::PipelineStageFlagBits::eColorAttachmentOutput
vk::AccessFlagBits::eColorAttachmentWrite
```

更完整的同步还会和 `acquireNextImageKHR` 返回的 image available semaphore 配合。  
项目在提交 graphics queue 时使用：

```cpp
constexpr vk::PipelineStageFlags waitDstStage =
    vk::PipelineStageFlagBits::eColorAttachmentOutput;

submit.setWaitSemaphores(imageAvaliable_)
      .setWaitDstStageMask(waitDstStage);
```

这表示 graphics queue 会等到 swapchain image 可用后，再在 color attachment output 阶段继续执行。  
Render pass dependency 和 queue submit semaphore 共同保证 attachment 写入时机正确。

---

### 1.2.8 创建 RenderPass：`vk::Device::createRenderPass`

目标：把 attachment、subpass、dependency 组合成 render pass 对象。  
当前项目的流程是：

```cpp
vk::RenderPassCreateInfo createInfo;
createInfo.setAttachments(attachDesc)
          .setSubpasses(subpass)
          .setDependencies(dependency);

renderPass = Context::GetInstance()
    .device
    .createRenderPass(createInfo);
```

`vk::RenderPass` 是 device 级对象，因此通过 `vk::Device` 创建和销毁。  
创建完成后，它会被两个地方使用：

1. 创建 framebuffer；
2. 创建 graphics pipeline；
3. 每帧 `beginRenderPass`。

对应当前项目：

```cpp
ContextInstance.renderProcess->InitRenderPass();
ContextInstance.swapchain->CreateFramebuffers(w, h);
ContextInstance.renderProcess->InitPipeline(w, h);
```

这个顺序不能随意打乱，因为 framebuffer 和 pipeline 都依赖 render pass。

# 2 Framebuffer 与 BeginRenderPass

## 2.1 概念

RenderPass 只描述 attachment 使用规则，不绑定具体图像。  
Framebuffer 才把具体的 `vk::ImageView` 绑定到 render pass 的 attachment 槽位上。

在当前项目中，swapchain 有多张 image，每张 image 有一个 image view，也就对应一个 framebuffer：

```text
swapchain image 0 -> image view 0 -> framebuffer 0
swapchain image 1 -> image view 1 -> framebuffer 1
swapchain image 2 -> image view 2 -> framebuffer 2
```

它们共用同一个 render pass。  
每一帧通过 `acquireNextImageKHR` 得到 image index，然后选中对应 framebuffer 开始 render pass。

## 2.2 Framebuffer 创建流程

### 2.2.1 绑定 ImageView：`vk::FramebufferCreateInfo::setAttachments`

目标：为 render pass 的 attachment 槽位提供实际 image view。  
当前项目每个 framebuffer 只有一个 color attachment：

```cpp
vk::FramebufferCreateInfo createInfo;
createInfo.setRenderPass(Context::GetInstance().renderProcess->renderPass)
          .setAttachments(imageViews[i])
          .setWidth(w)
          .setHeight(h)
          .setLayers(1);

framebuffers[i] =
    Context::GetInstance().device.createFramebuffer(createInfo);
```

关键参数：

1. `setRenderPass`：指定 framebuffer 要与哪个 render pass 配合使用；
2. `setAttachments`：提供具体 image view，顺序必须对应 render pass attachment 编号；
3. `setWidth / setHeight`：framebuffer 尺寸，通常与 swapchain extent 一致；
4. `setLayers(1)`：普通 2D 渲染通常为 1 层。

如果 render pass 声明了多个 attachment，framebuffer 就必须按相同顺序提供多个 image view：

```cpp
std::array attachments = {
    colorImageView,
    depthImageView
};

createInfo.setAttachments(attachments);
```

顺序必须和 render pass 中 attachment description 的编号一致。  
例如 color attachment 是 0，depth attachment 是 1，framebuffer 的 attachments 数组也要按这个顺序放。

## 2.3 BeginRenderPass 流程

### 2.3.1 填写 RenderPass Begin 信息：`vk::RenderPassBeginInfo`

目标：指定本帧使用哪个 render pass、哪个 framebuffer、渲染区域和 clear values。  
当前项目在每帧渲染中：

```cpp
vk::RenderPassBeginInfo renderPassBegin;
vk::Rect2D area;
vk::ClearValue clearValue;

clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
area.setOffset({0, 0})
    .setExtent(swapchain->info.imageExtent);

renderPassBegin.setRenderPass(renderProcess->renderPass)
               .setRenderArea(area)
               .setFramebuffer(swapchain->framebuffers[imageIndex])
               .setClearValues(clearValue);
```

`setFramebuffer(swapchain->framebuffers[imageIndex])` 是关键。  
`imageIndex` 来自：

```cpp
auto result = device.acquireNextImageKHR(
    swapchain->swapchain,
    std::numeric_limits<uint64_t>::max(),
    imageAvaliable_
);

auto imageIndex = result.value;
```

这表示当前 render pass 写入的是刚刚 acquire 到的那张 swapchain image。

---

### 2.3.2 开始和结束 RenderPass：`beginRenderPass` / `endRenderPass`

目标：在命令缓冲中声明渲染范围，所有 graphics draw call 都发生在 render pass 内部。

```cpp
cmdBuf_.beginRenderPass(renderPassBegin, {});
{
    cmdBuf_.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        renderProcess->pipeline
    );
    cmdBuf_.draw(3, 1, 0, 0);
}
cmdBuf_.endRenderPass();
```

`beginRenderPass` 会触发 attachment 的 load 行为和初始 layout 转换。  
例如当前 color attachment 会：

1. 从 `eUndefined` 转换到 subpass 使用的 `eColorAttachmentOptimal`；
2. 根据 `loadOp = eClear` 用 clear value 清空；
3. 在 subpass 中接收 fragment shader 输出。

`endRenderPass` 会触发 store 行为和 final layout 转换。  
例如当前 color attachment 会：

1. 根据 `storeOp = eStore` 保存渲染结果；
2. 从 `eColorAttachmentOptimal` 转换到 `ePresentSrcKHR`；
3. 后续可以交给 `presentKHR` 呈现。

# 3 RenderPass 与 Pipeline 的关系

## 3.1 概念

Graphics pipeline 创建时需要指定 render pass。  
这是因为 pipeline 的 fragment output、color blending、depth stencil 等状态必须和 render pass 的 attachment 描述兼容。

当前项目中：

```cpp
createInfo.setRenderPass(renderPass)
          .setLayout(layout);
```

这意味着该 pipeline 预期在与 `renderPass` 兼容的 render pass 实例中使用。  
如果绘制时 begin 的 render pass 与 pipeline 不兼容，就可能触发验证层错误或导致未定义行为。

## 3.2 Color Attachment 与 Fragment Output

Fragment shader 输出：

```glsl
layout(location = 0) out vec4 fragColor;
```

Subpass color attachment：

```cpp
subpass.setColorAttachments(reference);
```

Attachment reference：

```cpp
reference.setAttachment(0)
         .setLayout(vk::ImageLayout::eColorAttachmentOptimal);
```

这三者共同决定了 fragment shader 的输出路径：

```text
fragment shader location 0
    -> subpass color attachment 0
    -> render pass attachment 0
    -> framebuffer attachment 0
    -> swapchain image view
    -> swapchain image
```

如果 fragment shader 输出多个 location，subpass 也必须提供对应数量的 color attachments。  
如果 render pass 没有对应 attachment，或者格式/数量不兼容，pipeline 创建或绘制阶段会出错。

## 3.3 RenderPass 与 Color Blend

Pipeline 的 color blend state 也和 render pass attachment 数量有关。  
当前项目只有一个 color attachment，所以只设置一个 `vk::PipelineColorBlendAttachmentState`：

```cpp
vk::PipelineColorBlendAttachmentState attachs;
attachs.setBlendEnable(false)
       .setColorWriteMask(
           vk::ColorComponentFlagBits::eR |
           vk::ColorComponentFlagBits::eG |
           vk::ColorComponentFlagBits::eB |
           vk::ColorComponentFlagBits::eA
       );

vk::PipelineColorBlendStateCreateInfo blend;
blend.setAttachments(attachs);
```

如果 render pass 的 subpass 有多个 color attachments，blend state 通常也要提供多个 attachment blend 配置。  
每个 color attachment 都可以独立设置是否开启混合、写入哪些颜色通道、使用什么 blend factor。

## 3.4 RenderPass 与 Depth / Stencil

当前项目没有 depth attachment。  
如果后续加入深度测试，需要同时修改多个对象：

1. 创建 depth image；
2. 创建 depth image view；
3. render pass 增加 depth attachment description；
4. subpass 设置 depth stencil attachment reference；
5. framebuffer 增加 depth image view；
6. pipeline 增加 depth stencil state。

RenderPass 侧示例：

```cpp
vk::AttachmentDescription depthDesc;
depthDesc.setFormat(depthFormat)
         .setSamples(vk::SampleCountFlagBits::e1)
         .setLoadOp(vk::AttachmentLoadOp::eClear)
         .setStoreOp(vk::AttachmentStoreOp::eDontCare)
         .setInitialLayout(vk::ImageLayout::eUndefined)
         .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

vk::AttachmentReference depthRef;
depthRef.setAttachment(1)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

subpass.setPDepthStencilAttachment(&depthRef);
```

Pipeline 侧也要启用：

```cpp
vk::PipelineDepthStencilStateCreateInfo depthStencil;
depthStencil.setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthCompareOp(vk::CompareOp::eLess);

createInfo.setPDepthStencilState(&depthStencil);
```

只改 pipeline 或只改 render pass 都不够。  
Depth/stencil 是 image、image view、framebuffer、render pass、pipeline 共同参与的功能。

# 4 Subpass

## 4.1 概念

Subpass 是 render pass 内部的渲染阶段。  
一个 render pass 可以包含一个或多个 subpass。每个 subpass 可以使用不同的 attachment 组合，例如：

1. 第一个 subpass 写入 G-Buffer；
2. 第二个 subpass 读取 G-Buffer 作为 input attachment，并输出 lighting 结果；
3. 第三个 subpass 做后处理或 resolve。

当前项目只有一个 subpass，因此结构很简单：

```text
RenderPass
    -> Subpass 0
        -> color attachment 0
```

多 subpass 的优势是可以让驱动更清楚 attachment 之间的依赖和数据流，某些 tile-based GPU 上可能获得更好的内存利用。  
但多 subpass 也会增加 render pass 配置复杂度。入门阶段使用单 subpass 是合理选择。

## 4.2 Input Attachment

Input attachment 是 subpass 内部读取前面 subpass 输出的一种机制。  
它不同于普通纹理采样，通常用于 deferred rendering 等场景。

示意结构：

```text
Subpass 0:
    write color attachment 0
    write color attachment 1

Subpass 1:
    read input attachment 0
    read input attachment 1
    write color attachment 2
```

Subpass 1 中会通过 shader 读取 input attachment：

```glsl
layout(input_attachment_index = 0, set = 0, binding = 0)
uniform subpassInput gbufferAlbedo;
```

C++ 侧需要配置 input attachment reference、descriptor 等。  
当前项目还没有 deferred rendering，因此暂时不需要 input attachment。

## 4.3 Subpass Dependency

多 subpass 时，dependency 更重要。  
它用于描述前一个 subpass 写入 attachment 后，后一个 subpass 何时可以读取或继续写入。

例如第 0 个 subpass 写 color attachment，第 1 个 subpass 作为 input attachment 读取：

```cpp
vk::SubpassDependency dependency;
dependency.setSrcSubpass(0)
          .setDstSubpass(1)
          .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
          .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
          .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
          .setDstAccessMask(vk::AccessFlagBits::eInputAttachmentRead);
```

这表示：第 1 个 subpass 的 fragment shader 读取 input attachment 之前，必须等待第 0 个 subpass 的 color attachment 写入完成并可见。

# 5 RenderPass 生命周期与重建

## 5.1 生命周期

`vk::RenderPass` 是 device 级对象。  
创建时依赖 logical device，销毁时也通过 logical device：

```cpp
renderPass = device.createRenderPass(createInfo);
device.destroyRenderPass(renderPass);
```

RenderPass 被 framebuffer 和 pipeline 使用，因此推荐销毁顺序是：

```text
Pipeline
    -> Framebuffer
    -> RenderPass
```

更完整的 swapchain 相关销毁顺序通常是：

```text
device.waitIdle
    -> Renderer / Command resources
    -> Pipeline
    -> Framebuffer
    -> RenderPass
    -> ImageView
    -> Swapchain
```

核心原则是：先销毁使用 render pass 的对象，再销毁 render pass 本身。  
同时要保证 GPU 不再执行使用这些对象的命令，最简单方式是在重建或退出前调用：

```cpp
device.waitIdle();
```

## 5.2 何时需要重建 RenderPass

RenderPass 不一定每次窗口 resize 都必须重建，但以下情况通常需要：

1. swapchain image format 改变；
2. color/depth attachment 数量改变；
3. attachment load/store op 改变；
4. attachment sample count 改变；
5. 是否使用 depth/stencil 改变；
6. subpass 结构改变；
7. attachment final layout 或使用方式改变。

窗口 resize 时，如果只是 extent 改变，而 swapchain format、attachment 配置、render pass 结构都不变，render pass 理论上可以复用。  
但 framebuffer 一定需要随 swapchain image view 和 extent 重建。

当前项目的初始化链路中，render pass 使用了 swapchain format：

```cpp
attachDesc.setFormat(Context::GetInstance().swapchain->info.format.format);
```

因此如果重建 swapchain 时 format 改变，render pass 也要重建，依赖该 render pass 的 pipeline 也要重建。

## 5.3 实践建议

1. 先用单 color attachment + 单 subpass 跑通基础渲染。
2. swapchain color attachment 通常使用 `loadOp = eClear`、`storeOp = eStore`。
3. 如果每帧覆盖整张画面，`initialLayout = eUndefined` 是合理选择。
4. swapchain image 渲染结束后通常使用 `finalLayout = ePresentSrcKHR`。
5. `AttachmentReference` 的 layout 描述 subpass 内部使用布局，不是 final layout。
6. fragment shader output location 要和 subpass color attachment 顺序对应。
7. framebuffer attachments 的顺序必须和 render pass attachment 编号一致。
8. 加入 depth test 时，要同时修改 depth image、framebuffer、render pass 和 pipeline。
9. 多 subpass 不适合过早引入，先理解单 subpass 的 attachment 和 layout 关系。
10. 重建 swapchain 时，重点检查 render pass 是否受 format、sample count、attachment 数量影响。
11. 销毁前确保 GPU 不再使用 render pass，退出阶段可以使用 `device.waitIdle()`。
12. 验证层对 render pass、framebuffer、pipeline 不兼容问题非常有帮助，开发阶段应保持开启。

## 5.4 当前项目的 RenderPass 链路

Toy2D 当前 render pass 链路可以概括为：

```text
Swapchain::queryInfo
    -> 取得 swapchain format
    -> RenderProcess::InitRenderPass
    -> 创建 color attachment description
    -> 创建 attachment reference
    -> 创建 subpass
    -> 创建 subpass dependency
    -> device.createRenderPass
    -> Swapchain::CreateFramebuffers
    -> RenderProcess::InitPipeline
    -> Renderer::Render
    -> cmdBuf.beginRenderPass
    -> bindPipeline + draw
    -> cmdBuf.endRenderPass
    -> presentKHR
```

这条链路说明了 RenderPass 在 Vulkan 初始化中的位置：  
它位于 swapchain 之后、framebuffer 和 pipeline 之前，是连接“图像资源”和“渲染管线”的关键对象。
