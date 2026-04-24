# 1 Vulkan 中的深度缓冲

## 1.1 概念

### 1.1.1 深度缓冲的作用

颜色附件只负责保存最终颜色，并不负责判断“谁在前面”。如果场景里有两个物体在屏幕空间上发生重叠，而渲染流程又没有额外的可见性判断机制，那么最终结果只取决于提交顺序：

1. 先画的物体先写入颜色附件；
2. 后画的物体直接覆盖前面的像素；
3. 即使它在空间里更远，仍然可能显示在最上层。

深度缓冲的作用正是把“遮挡关系判断”交给 GPU 的固定功能阶段处理。每个片元都会带着一个深度值进入测试阶段，GPU 将其与深度附件中已经保存的值进行比较，只保留更符合比较规则的那个片元。

---

### 1.1.2 深度缓冲保存的数据

深度缓冲本质上是一张 2D image，只不过它的像素格式不是颜色格式，而是深度格式，例如：

1. `vk::Format::eD32Sfloat`
2. `vk::Format::eD32SfloatS8Uint`
3. `vk::Format::eD24UnormS8Uint`

每个像素保存的是该屏幕位置当前通过测试的深度值。当前工程的投影矩阵使用的是 Vulkan 风格的 `[0, 1]` 深度范围，因此：

1. `0.0` 表示更靠近相机；
2. `1.0` 表示更远离相机。

这也是为什么深度缓冲通常在每帧开始时清为 `1.0f`：初始值先设成“最远”，后续真正绘制出来的片元只要更近，就可以通过深度测试并写入新的深度值。

---

### 1.1.3 深度缓冲的完整链路

深度测试能否工作，不取决于单个资源或单个状态，而是取决于多处配置是否同时成立：

1. 需要一个可作为 `DepthStencilAttachment` 使用的 image；
2. 需要一个指向该 image 的 image view；
3. RenderPass 需要声明 depth attachment；
4. Subpass 需要把 depth attachment 接入图形管线；
5. Graphics Pipeline 需要开启 depth test 和 depth write；
6. Framebuffer 需要在 attachment 列表里同时放入颜色 view 与深度 view；
7. BeginRenderPass 时需要提供 depth clear value。

这条链路缺任何一个环节，深度缓冲都不会真正生效。

---

### 1.1.4 当前工程中的职责划分

当前工程把深度资源放在 `Swapchain` 中，把深度测试状态放在 `RenderProcess` 中，这种划分与 Vulkan 的资源关系是匹配的。

`Swapchain` 负责：

1. 记录深度格式；
2. 创建 `vk::Image`；
3. 分配并绑定 `vk::DeviceMemory`；
4. 创建 `vk::ImageView`；
5. 在创建 framebuffer 时把深度 view 一起挂进去。

`RenderProcess` 负责：

1. 在 render pass 中声明 depth attachment；
2. 在 subpass 中把 depth attachment 接入；
3. 在 graphics pipeline 中开启 depth test、depth write 和比较函数。

这种拆分让“资源所有权”和“渲染行为配置”保持清晰分离。

---

### 1.1.5 两个容易混淆的 depth

深度缓冲实现里经常同时出现两个“depth”，含义并不相同。

第一个是 image 尺寸里的第三维，例如：

```cpp
imageInfo.setImageType(vk::ImageType::e2D)
         .setExtent(vk::Extent3D{GetExtent().width, GetExtent().height, 1});
```

这里的第三个参数表示 image 的体积厚度。当前工程创建的是 `e2D` 图像，所以第三维固定写 `1`。

第二个是深度附件中的“深度值”，也就是每个片元用于比较前后关系的 Z 值。它来自投影变换后的片元深度，与 `Extent3D` 的第三维没有直接关系。

---

## 1.2 流程

### 1.2.1 Vulkan 的 `[0, 1]` 深度范围

当前工程的相机没有直接调用 `glm::perspective`，而是手写了一版 `CreatePerspectiveRH_ZO`。对应接口在 `src/camera.cpp` 中：

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

这里的 `ZO` 表示 zero-to-one。对应当前工程的深度语义：

1. 近平面附近的深度值更小；
2. 远平面附近的深度值更大；
3. `vk::CompareOp::eLess` 正好表示“更近的片元通过测试”。

如果投影空间与深度比较函数不匹配，后续即使深度附件已经接入，遮挡关系依然会整体错误。

---

### 1.2.2 Swapchain 中的深度资源

当前工程在 `toy2d/swapchain.hpp` 中直接保存深度附件资源：

```cpp
class Swapchain final {
public:
    vk::SurfaceKHR surface = nullptr;
    vk::SwapchainKHR swapchain = nullptr;
    std::vector<ImageResource> swapchainImages;

    AllocatedImage depthImage;

    std::vector<vk::Framebuffer> framebuffers;
};
```

这组成员的含义分别是：

1. `depthImage.image` 保存真正的 Vulkan image；
2. `depthImage.memory` 保存该 image 绑定的显存；
3. `depthImage.view` 保存深度附件对应的 image view；
4. `depthImage.format` 用于 render pass 和 image view 的格式匹配。

把深度资源与 swapchain 放在一起，最大的好处是它们的尺寸与生命周期天然一致：交换链重建时，深度附件也应该一起重建。

---

### 1.2.3 Swapchain 构造阶段的深度资源创建

当前工程在 `Swapchain` 构造函数里先创建交换链图像视图，再创建深度附件：

```cpp
Swapchain::Swapchain(vk::SurfaceKHR surface, int windowWidth, int windowHeight): surface(surface) {
    querySurfaceInfo(windowWidth, windowHeight);
    swapchain = createSwapchain();
    createImageAndViews();
    createDepthResource();
}
```

这个顺序有明确依赖关系：

1. `querySurfaceInfo` 先确定交换链尺寸和颜色格式；
2. `createSwapchain` 拿到真正的 swapchain；
3. `createImageAndViews` 为交换链图像创建颜色 view；
4. `createDepthResource` 依据 swapchain extent 创建同尺寸的深度 image。

深度附件必须与当前 framebuffer 尺寸一致，否则 framebuffer 创建阶段会直接失败。

---

### 1.2.4 深度格式查询

深度附件不能写死格式，必须由物理设备能力决定。当前工程通过 `findDepthFormat()` 在一组候选格式中寻找支持 `eDepthStencilAttachment` 的格式：

```cpp
vk::Format Swapchain::findDepthFormat() const {
    std::array<vk::Format, 3> candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint
    };

    auto& phy = Context::Instance().phyDevice;
    for (const auto& format : candidates) {
        auto props = phy.getFormatProperties(format);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format!");
}
```

这里检查的是 `optimalTilingFeatures`，因为当前工程创建 depth image 时使用的是 `vk::ImageTiling::eOptimal`。如果 tiling 与格式能力检查不一致，运行时依然可能出错。

---

### 1.2.5 深度 image 的创建

真正的深度资源创建发生在 `Swapchain::createDepthResource()` 中。对应的核心接口是 `vk::ImageCreateInfo`：

```cpp
vk::ImageCreateInfo imageInfo;
imageInfo.setImageType(vk::ImageType::e2D)
         .setExtent(vk::Extent3D{GetExtent().width, GetExtent().height, 1})
         .setMipLevels(1)
         .setArrayLayers(1)
         .setFormat(depthImage.format)
         .setTiling(vk::ImageTiling::eOptimal)
         .setInitialLayout(vk::ImageLayout::eUndefined)
         .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
         .setSamples(vk::SampleCountFlagBits::e1)
         .setSharingMode(vk::SharingMode::eExclusive);
depthImage.image = ctx.device.createImage(imageInfo);
```

这里几个参数最值得关注：

1. `setImageType(vk::ImageType::e2D)`  
   深度缓冲是 2D 附件，不是 3D 纹理。

2. `setExtent(vk::Extent3D{width, height, 1})`  
   `vk::Image` 的创建接口统一使用 `Extent3D`，即使是 2D 图像也必须传第三维，只是该值固定为 `1`。

3. `setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)`  
   明确告诉驱动这张 image 将作为深度/模板附件使用。

4. `setInitialLayout(vk::ImageLayout::eUndefined)`  
   表示初始内容无意义，后续在 render pass 中通过 `loadOp = eClear` 重新初始化。

---

### 1.2.6 深度 image 的显存分配与绑定

image 创建完成后，必须先查询内存需求，再分配 `vk::DeviceMemory` 并执行绑定。当前工程的实现如下：

```cpp
auto memReq = ctx.device.getImageMemoryRequirements(depthImage.image);
vk::MemoryAllocateInfo allocInfo;
allocInfo.setAllocationSize(memReq.size)
         .setMemoryTypeIndex(
             queryBufferMemTypeIndex(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
depthImage.memory = ctx.device.allocateMemory(allocInfo);
ctx.device.bindImageMemory(depthImage.image, depthImage.memory, 0);
```

这里复用了工程里已有的 `queryBufferMemTypeIndex(...)` 帮助函数。虽然名字里带 `Buffer`，但它本质上只是根据 `memoryTypeBits` 和目标属性筛选内存类型，因此同样可以服务于 image 分配。

`vk::MemoryPropertyFlagBits::eDeviceLocal` 的选择也很自然：深度附件不会像 uniform buffer 那样被 CPU 高频更新，它的使用场景就是留在设备本地内存中供 GPU 读写。

---

### 1.2.7 深度 image view 的创建

depth image 不能直接挂到 framebuffer，framebuffer 需要的是 image view。当前工程在创建 view 时显式指定了 `eDepth` aspect：

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eDepth)
     .setBaseMipLevel(0)
     .setLevelCount(1)
     .setBaseArrayLayer(0)
     .setLayerCount(1);

vk::ImageViewCreateInfo viewInfo;
viewInfo.setImage(depthImage.image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(depthImage.format)
        .setSubresourceRange(range);
depthImage.view = ctx.device.createImageView(viewInfo);
```

`aspectMask` 不能写成颜色附件使用的 `eColor`。深度附件的 view 必须暴露深度子资源，否则 framebuffer 与 render pass 在 attachment 类型上会不匹配。

当前工程还没有使用 stencil test，因此这里只选择了 `eDepth`。如果后续启用 stencil 功能，再根据格式补充 stencil aspect 的处理逻辑即可。

---

### 1.2.8 RenderPass 中的深度附件声明

资源准备好之后，下一步是让 RenderPass 知道这次渲染除了颜色附件外，还需要一个深度附件。当前工程在 `src/render_process.cpp` 中同时定义了颜色 attachment 和深度 attachment。

颜色 attachment：

```cpp
vk::AttachmentDescription colorAttachment;
colorAttachment.setFormat(ctx.swapchain->GetFormat().format)
               .setSamples(vk::SampleCountFlagBits::e1)
               .setLoadOp(vk::AttachmentLoadOp::eClear)
               .setStoreOp(vk::AttachmentStoreOp::eStore)
               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
               .setInitialLayout(vk::ImageLayout::eUndefined)
               .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
```

深度 attachment：

```cpp
vk::AttachmentDescription depthAttachment;
depthAttachment.setFormat(ctx.swapchain->GetDepthFormat())
               .setSamples(vk::SampleCountFlagBits::e1)
               .setLoadOp(vk::AttachmentLoadOp::eClear)
               .setStoreOp(vk::AttachmentStoreOp::eDontCare)
               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
               .setInitialLayout(vk::ImageLayout::eUndefined)
               .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
```

这两段配置体现了当前工程的典型策略：

1. 颜色附件需要最终呈现到屏幕，因此 `storeOp = eStore`；
2. 深度附件这一帧结束后不需要保留给展示阶段，因此 `storeOp = eDontCare`；
3. 两者都在 subpass 开始前清空，因此 `loadOp = eClear`。

---

### 1.2.9 Subpass 中的深度附件接入

定义 attachment 只是第一步，真正把它接入图形子通道的是 `vk::AttachmentReference` 与 `vk::SubpassDescription`：

```cpp
vk::AttachmentReference colorRef;
colorRef.setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

vk::AttachmentReference depthRef;
depthRef.setAttachment(1)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

vk::SubpassDescription subpassDesc;
subpassDesc.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
           .setColorAttachments(colorRef)
           .setPDepthStencilAttachment(&depthRef);
```

这里的 attachment index 非常关键：

1. `attachment(0)` 对应颜色附件；
2. `attachment(1)` 对应深度附件。

它们必须与 `createInfo.setAttachments(attachments)` 中数组的顺序严格一致。顺序不一致时，验证层通常会直接报 attachment 类型或格式不匹配。

---

### 1.2.10 深度写入的同步阶段

当前工程的 subpass dependency 已经把颜色输出阶段和早期片元测试阶段一起纳入同步范围：

```cpp
vk::SubpassDependency dependency;
dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
    .setDstSubpass(0)
    .setSrcStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput
        | vk::PipelineStageFlagBits::eEarlyFragmentTests)
    .setDstStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput
        | vk::PipelineStageFlagBits::eEarlyFragmentTests)
    .setDstAccessMask(
        vk::AccessFlagBits::eColorAttachmentWrite
        | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
```

深度测试通常发生在 `EarlyFragmentTests` 阶段，因此同步若只覆盖 `ColorAttachmentOutput`，语义就不完整。当前写法保证颜色附件写入和深度附件写入都处在 RenderPass 明确声明的依赖链中。

---

### 1.2.11 Graphics Pipeline 中的深度测试

RenderPass 决定“有深度附件”，而 Graphics Pipeline 决定“怎样使用深度附件”。当前工程通过 `vk::PipelineDepthStencilStateCreateInfo` 启用深度测试：

```cpp
vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
depthStencilInfo.setDepthTestEnable(true)
                .setDepthWriteEnable(true)
                .setDepthCompareOp(vk::CompareOp::eLess)
                .setDepthBoundsTestEnable(false)
                .setStencilTestEnable(false);
```

每个字段的含义都很明确：

1. `setDepthTestEnable(true)`  
   让片元进入深度比较流程。

2. `setDepthWriteEnable(true)`  
   比较通过后，把新的深度值写回深度缓冲。

3. `setDepthCompareOp(vk::CompareOp::eLess)`  
   只有更小的深度值才能通过，结合 `[0, 1]` 深度空间，等价于“更靠近相机的片元通过”。

最后把这个状态挂到 `vk::GraphicsPipelineCreateInfo`：

```cpp
createInfo.setPDepthStencilState(&depthStencilInfo);
```

如果漏掉这一步，即使前面的 depth image、render pass、framebuffer 都已经配置正确，深度缓冲依然不会起作用。

---

### 1.2.12 Framebuffer 中的颜色 view 与深度 view

Framebuffer 是 attachment 绑定到具体 image view 的地方。当前工程为每个 swapchain image 创建一个 framebuffer，并复用同一个深度 view：

```cpp
std::array<vk::ImageView, 2> attachments = {
    img.view,
    depthImage.view
};

createInfo.setAttachments(attachments)
          .setLayers(1)
          .setHeight(GetExtent().height)
          .setWidth(GetExtent().width)
          .setRenderPass(Context::Instance().renderProcess->renderPass);
```

这里的结构关系是：

1. `img.view` 是当前交换链图像的颜色 view；
2. `depthImage.view` 是当前 swapchain 共享的深度 view；
3. attachment 顺序与 render pass 中的 attachment description 顺序一一对应。

这一步完成之后，RenderPass 声明的 attachment 才真正拥有了可写入的实际图像资源。

---

### 1.2.13 BeginRenderPass 时的颜色与深度清理

当前工程在 `Renderer::BeginFrame()` 中传入了两个 clear value，对应颜色附件与深度附件：

```cpp
std::array<vk::ClearValue, 2> clearValues;
clearValues[0].setColor(vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}));
clearValues[1].setDepthStencil(vk::ClearDepthStencilValue(1.0f, 0));

vk::RenderPassBeginInfo renderPassBegin;
renderPassBegin.setRenderPass(ctx.renderProcess->renderPass)
               .setFramebuffer(swapchain->framebuffers[imageIndex_])
               .setClearValues(clearValues)
               .setRenderArea(vk::Rect2D({}, swapchain->GetExtent()));
```

这里的 `vk::ClearDepthStencilValue(1.0f, 0)` 与当前投影空间完全匹配：

1. 深度清成 `1.0f`，表示初始时所有像素都处于最远位置；
2. 后续片元只要更近，就能通过 `eLess` 比较；
3. stencil 当前未使用，因此保持 `0` 即可。

clear value 的数量必须和 attachment 数量一致。当前 RenderPass 有两个 attachment，因此 `clearValues` 也必须正好提供两个元素。

---

### 1.2.14 资源销毁顺序与 attachment 依赖

当前工程在 `Swapchain::~Swapchain()` 中先销毁 framebuffer，再销毁深度资源：

```cpp
for (auto& framebuffer : framebuffers) {
    Context::Instance().device.destroyFramebuffer(framebuffer);
}

if (depthImage.view) {
    ctx.device.destroyImageView(depthImage.view);
}

if (depthImage.image) {
    ctx.device.destroyImage(depthImage.image);
}

if (depthImage.memory) {
    ctx.device.freeMemory(depthImage.memory);
}
```

这个顺序是合理的。Framebuffer 持有 attachment view 的引用关系，因此应该先销毁 framebuffer，再销毁它所依赖的 image view 和 image 本体。

---

## 1.3 当前实现的效果与边界

### 1.3.1 当前实现已经解决的问题

在当前工程里接入深度缓冲之后，渲染系统第一次具备了稳定的空间遮挡判断能力：

1. 更远的物体不会因为后提交而错误盖住近处物体；
2. 多个 3D 位置不同的四边形或立方体可以按空间深度正确遮挡；
3. 相机移动后，前后关系能够随视角变化保持稳定。

这一步使当前工程从“具备 3D 相机观察能力”进一步迈向“真正按 3D 空间关系进行可见性裁决”。

---

### 1.3.2 当前实现保留的简化

当前实现仍然是一个入门版本，保留了若干简化策略：

1. 只创建了一份共享的深度附件，没有做更复杂的多重缓冲拆分；
2. 没有显式处理 stencil 逻辑；
3. 没有加入 MSAA；
4. 还没有展示窗口 resize 后的 swapchain 与 depth attachment 重建流程。

这些简化不影响理解深度缓冲的主链路，但后续继续扩展时需要把它们纳入考虑。

---

### 1.3.3 容易遗漏的几个点

1. 只创建深度 image，不在 RenderPass 中声明 attachment；  
   结果是深度资源根本没有进入子通道。

2. 只在 RenderPass 中声明深度附件，不在 Pipeline 中开启深度测试；  
   结果是 depth attachment 存在，但片元不会进行深度比较。

3. 深度 clear 值写成 `0.0f`；  
   结果是在 `eLess` 比较下，大量本应可见的片元直接失败。

4. Framebuffer 中 attachment 顺序与 RenderPass 中描述顺序不一致；  
   结果通常是验证层直接报错。

5. 投影矩阵不是 Vulkan 的 `[0, 1]` 深度空间，却仍然使用 `eLess` 和 `1.0f` 清深度；  
   结果是深度语义与比较规则脱节。

---

## 1.4 总结

当前工程中的深度缓冲实现可以归纳为一条非常清晰的链路：

1. 相机输出 Vulkan 风格的 `[0, 1]` 深度；
2. `Swapchain` 创建 depth image、memory 与 image view；
3. `RenderProcess` 在 RenderPass 中声明 depth attachment；
4. Graphics Pipeline 开启 depth test 与 depth write；
5. Framebuffer 同时绑定颜色 view 与深度 view；
6. `Renderer::BeginFrame()` 在每帧开始时把深度清为 `1.0f`。

这条链路建立之后，渲染系统的遮挡关系才真正从“提交顺序决定”变成“空间深度决定”。对 3D 场景来说，深度缓冲不是可选优化，而是最基础的可见性机制之一。

---

参考教程：<https://vulkan-tutorial.com/Depth_buffering>
