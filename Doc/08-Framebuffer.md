# 1 Framebuffer

## 1.1 概念

`vk::Framebuffer`是 render pass 执行时绑定的具体图像集合。  
它不描述 attachment 应该如何被加载、保存、布局转换；这些规则属于 `vk::RenderPass`。Framebuffer 的职责是把一组实际的 `vk::ImageView` 填入 render pass 声明的 attachment 槽位中。

可以把关系理解为：

```text
RenderPass
    -> 描述 attachment 规则

Framebuffer
    -> 绑定实际 image view
```

在当前项目中，render pass 声明了一个 color attachment。  
swapchain 中有多张 image，每张 image 创建一个 `vk::ImageView`，然后每个 image view 创建一个对应的 framebuffer：

```text
swapchain image 0 -> image view 0 -> framebuffer 0
swapchain image 1 -> image view 1 -> framebuffer 1
swapchain image 2 -> image view 2 -> framebuffer 2
```

每一帧通过 `acquireNextImageKHR` 得到当前可渲染的 `imageIndex`。  
然后在 `beginRenderPass` 时选择：

```cpp
swapchain->framebuffers[imageIndex]
```

这意味着当前帧的 render pass 会写入该 index 对应的 swapchain image。

Framebuffer 的核心职责：

1. 指定 render pass 本次使用哪些具体 image views；
2. 保证 attachment 数量和顺序与 render pass 兼容；
3. 指定 framebuffer 的宽、高、层数；
4. 作为 `vk::RenderPassBeginInfo` 的输入，让命令缓冲开始一次具体渲染。

常见误区：

1. **把 framebuffer 当成 image**：framebuffer 不存储像素，真正存储像素的是 image。
2. **把 framebuffer 当成 image view**：image view 是单张 image 的访问视图，framebuffer 是一个或多个 image views 的集合。
3. **把 framebuffer 和 render pass 混淆**：render pass 描述规则，framebuffer 绑定资源。
4. **只给 swapchain 创建一个 framebuffer**：swapchain 有多张 image，通常每张 swapchain image view 都需要一个 framebuffer。
5. **窗口 resize 后继续使用旧 framebuffer**：framebuffer 依赖 image view 和尺寸，swapchain 重建后通常必须重建 framebuffer。

## 1.2 创建流程

### 1.2.1 确认 RenderPass Attachment 规则：`vk::RenderPass`

目标：先确定 framebuffer 要匹配哪个 render pass。  
Framebuffer 必须和 render pass 兼容，因为 framebuffer 提供的 image view 数量、格式、尺寸等要满足 render pass attachment 的要求。

当前项目 render pass 只有一个 color attachment：

```cpp
vk::AttachmentDescription attachDesc;
attachDesc.setFormat(Context::GetInstance().swapchain->info.format.format)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR)
          .setLoadOp(vk::AttachmentLoadOp::eClear)
          .setStoreOp(vk::AttachmentStoreOp::eStore)
          .setSamples(vk::SampleCountFlagBits::e1);

vk::RenderPassCreateInfo createInfo;
createInfo.setAttachments(attachDesc);
```

这表示后续 framebuffer 至少要提供一个与该 attachment 兼容的 color image view。  
当前项目中，这个 image view 来自 swapchain image。

---

### 1.2.2 准备 ImageView：`vk::ImageView`

目标：为 framebuffer 提供具体 attachment 资源。  
Framebuffer 不直接绑定 `vk::Image`，而是绑定 `vk::ImageView`。

当前项目先从 swapchain 获取 images：

```cpp
images = Context::GetInstance()
    .device
    .getSwapchainImagesKHR(swapchain);
```

然后为每张 image 创建 image view：

```cpp
vk::ImageViewCreateInfo createInfo;
createInfo.setImage(images[i])
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(info.format.format)
          .setSubresourceRange(range);

imageViews[i] =
    Context::GetInstance().device.createImageView(createInfo);
```

这里的 `imageViews[i]` 就是后续 framebuffer 的 attachment。  
如果 render pass 有多个 attachment，例如 color + depth，那么 framebuffer 也要准备多个 image views。

---

### 1.2.3 填写 Framebuffer 创建信息：`vk::FramebufferCreateInfo`

目标：把 render pass、attachments、尺寸和层数写入 framebuffer 创建参数。

当前项目的 `Swapchain::CreateFramebuffers`：

```cpp
void Swapchain::CreateFramebuffers(int w, int h) {
    framebuffers.resize(images.size());

    for (int i = 0; i < framebuffers.size(); ++i) {
        vk::FramebufferCreateInfo createInfo;
        createInfo.setAttachments(imageViews[i])
                  .setWidth(w)
                  .setHeight(h)
                  .setRenderPass(Context::GetInstance().renderProcess->renderPass)
                  .setLayers(1);

        framebuffers[i] =
            Context::GetInstance().device.createFramebuffer(createInfo);
    }
}
```

关键参数说明：

1. `setRenderPass`：指定这个 framebuffer 要配合哪个 render pass 使用；
2. `setAttachments`：绑定实际 image views；
3. `setWidth / setHeight`：framebuffer 尺寸；
4. `setLayers`：framebuffer 层数，普通 2D 窗口渲染通常为 1。

在当前项目中，`setAttachments(imageViews[i])` 只有一个 image view，因为 render pass 只有一个 color attachment。  
如果 render pass 声明了多个 attachment，就需要传入数组：

```cpp
std::array attachments = {
    colorImageView,
    depthImageView
};

createInfo.setAttachments(attachments);
```

attachments 数组顺序必须与 render pass 中 attachment 编号一致。  
例如 render pass 中 attachment 0 是 color，attachment 1 是 depth，那么 framebuffer 中也必须按 `{ colorView, depthView }` 的顺序提供。

---

### 1.2.4 创建 Framebuffer：`vk::Device::createFramebuffer`

目标：在 logical device 上创建 framebuffer 对象。

```cpp
vk::Framebuffer framebuffer =
    Context::GetInstance().device.createFramebuffer(createInfo);
```

`vk::Framebuffer` 是 device 级对象。  
它依赖：

1. logical device；
2. render pass；
3. image views；
4. framebuffer width / height / layers。

创建完成后，它会在每帧渲染时作为 `vk::RenderPassBeginInfo` 的一部分传入命令缓冲。

---

### 1.2.5 为每个 Swapchain Image 创建一个 Framebuffer

目标：让每张 swapchain image 都能作为一帧的渲染目标。  
swapchain image 数量不一定固定为 2 或 3，应以 `getSwapchainImagesKHR` 返回结果为准。

```cpp
framebuffers.resize(images.size());

for (std::size_t i = 0; i < images.size(); ++i) {
    vk::FramebufferCreateInfo createInfo;
    createInfo.setRenderPass(renderPass)
              .setAttachments(imageViews[i])
              .setWidth(info.imageExtent.width)
              .setHeight(info.imageExtent.height)
              .setLayers(1);

    framebuffers[i] = device.createFramebuffer(createInfo);
}
```

这种“一张 swapchain image view 对应一个 framebuffer”的设计，是窗口渲染中最常见的写法。  
原因是每帧 acquire 到的 swapchain image index 不固定，应用需要能够按 index 找到对应的 framebuffer。

# 2 Framebuffer 使用流程

## 2.1 概念

Framebuffer 的使用发生在 command buffer 录制阶段。  
它不会单独绑定到 pipeline，也不会直接提交给 queue，而是在 `beginRenderPass` 时和 render pass 一起指定。

典型流程：

```text
acquireNextImageKHR
    -> 得到 imageIndex
    -> 选择 framebuffers[imageIndex]
    -> beginRenderPass
    -> bindPipeline
    -> draw
    -> endRenderPass
    -> present imageIndex
```

这条链路的关键是：`imageIndex` 同时用于选择 framebuffer 和提交 present。  
这样才能保证“渲染写入的图像”和“最终呈现的图像”是同一张 swapchain image。

## 2.2 每帧选择 Framebuffer

### 2.2.1 获取 Swapchain Image：`vk::Device::acquireNextImageKHR`

目标：从 swapchain 中取得本帧可以渲染的图像索引。

```cpp
auto result = device.acquireNextImageKHR(
    swapchain->swapchain,
    std::numeric_limits<uint64_t>::max(),
    imageAvaliable_
);

auto imageIndex = result.value;
```

`imageIndex` 是 swapchain image 数组索引，不是帧号。  
它用于定位：

1. 当前要渲染到哪张 swapchain image；
2. 当前要使用哪个 image view；
3. 当前要使用哪个 framebuffer；
4. present 时提交哪张 image。

---

### 2.2.2 写入 RenderPass Begin 信息：`vk::RenderPassBeginInfo::setFramebuffer`

目标：告诉 render pass 本次具体写哪个 framebuffer。  
当前项目使用：

```cpp
vk::RenderPassBeginInfo renderPassBegin;
renderPassBegin.setRenderPass(renderProcess->renderPass)
               .setRenderArea(area)
               .setFramebuffer(swapchain->framebuffers[imageIndex])
               .setClearValues(clearValue);
```

这里的 `setFramebuffer(swapchain->framebuffers[imageIndex])` 是 framebuffer 真正进入渲染流程的位置。  
它把 render pass 中抽象的 attachment 0 绑定到当前 framebuffer 中的实际 image view。

如果当前 imageIndex 是 1，那么本帧写入：

```text
framebuffers[1]
    -> imageViews[1]
    -> images[1]
```

后续 present 也应提交同一个 `imageIndex`。

---

### 2.2.3 开始 RenderPass：`vk::CommandBuffer::beginRenderPass`

目标：开始一次具体 render pass 实例。  
RenderPass 描述规则，Framebuffer 提供具体 image view，二者在 `beginRenderPass` 时组合起来。

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

`beginRenderPass` 后，当前 framebuffer 的 attachments 会根据 render pass 描述执行：

1. attachment layout transition；
2. loadOp，例如 clear；
3. subpass 中作为 color/depth attachment 被写入；
4. endRenderPass 时执行 storeOp；
5. 转换到 final layout。

在当前项目中，framebuffer 里的 swapchain image view 会先被清成灰色背景，然后 fragment shader 输出的红色三角形写入同一张 image。

## 2.3 Framebuffer 与 Present

### 2.3.1 Framebuffer 不负责 Present

Framebuffer 只是 render pass 的渲染目标绑定对象，不负责把图像显示到屏幕。  
真正提交显示的是 present queue：

```cpp
vk::PresentInfoKHR present;
present.setImageIndices(imageIndex)
       .setSwapchains(swapchain->swapchain)
       .setWaitSemaphores(imageDrawFinish_[imageIndex]);

Context::GetInstance().presentQueue.presentKHR(present);
```

Framebuffer 参与的是“写入哪张图像”，present 参与的是“显示哪张图像”。  
二者通过同一个 `imageIndex` 连接：

```text
framebuffers[imageIndex] 被渲染写入
imageIndex 被 present 提交显示
```

如果这两个 index 不一致，就会出现渲染结果和显示图像错位的问题。

# 3 Framebuffer 与其他对象的关系

## 3.1 Framebuffer 与 RenderPass

Framebuffer 必须指定一个 render pass：

```cpp
createInfo.setRenderPass(renderPass);
```

这表示 framebuffer 的 attachments 要满足该 render pass 的要求。  
例如 render pass 声明：

1. attachment 0：color，格式为 swapchain format；
2. samples 为 `e1`；
3. subpass 中作为 color attachment 使用。

那么 framebuffer 的 attachment 0 就必须是兼容的 color image view。  
如果 image view format、sample count、attachment 数量不匹配，创建 framebuffer 或后续 begin render pass 时会触发验证层错误。

RenderPass 与 Framebuffer 的分工：

```text
RenderPass:
    attachment 0 是什么规则

Framebuffer:
    attachment 0 具体是哪张 image view
```

这也是为什么同一个 render pass 可以搭配多个 framebuffer：规则相同，但具体 image view 不同。

## 3.2 Framebuffer 与 ImageView

Framebuffer 绑定的是 image view，不是 image。  
原因是 render pass attachment 需要知道访问 image 的哪一部分，例如：

1. color aspect 还是 depth aspect；
2. 哪个 mip level；
3. 哪个 array layer；
4. 以 2D、2D array、cube 等哪种 view type 访问。

当前项目中 image view 的 subresource range 是：

```cpp
range.setBaseMipLevel(0)
     .setLevelCount(1)
     .setBaseArrayLayer(0)
     .setLayerCount(1)
     .setAspectMask(vk::ImageAspectFlagBits::eColor);
```

这表示 framebuffer 绑定的是 swapchain image 的 color aspect、mip 0、layer 0。  
对于普通窗口渲染，这是最常见配置。

## 3.3 Framebuffer 与 Swapchain

窗口渲染中，framebuffer 通常直接依赖 swapchain：

```text
Swapchain
    -> Images
    -> ImageViews
    -> Framebuffers
```

Swapchain 重建时，旧 swapchain images 失效，旧 image views 也要销毁，因此旧 framebuffers 也必须销毁。  
重建顺序通常是：

```text
device.waitIdle
    -> destroy old framebuffers
    -> destroy old image views
    -> destroy old swapchain
    -> create new swapchain
    -> get new images
    -> create new image views
    -> create new framebuffers
```

如果 render pass 的 attachment format 没变，render pass 可能可以复用。  
但 framebuffer 依赖具体 image view 和尺寸，因此 swapchain 重建后 framebuffer 基本一定要重建。

## 3.4 Framebuffer 与 Pipeline

Pipeline 不直接保存 framebuffer。  
Graphics pipeline 创建时绑定的是 render pass，而不是某个具体 framebuffer：

```cpp
createInfo.setRenderPass(renderPass)
          .setLayout(layout);
```

命令缓冲录制时：

1. `beginRenderPass` 指定 framebuffer；
2. `bindPipeline` 指定 pipeline；
3. pipeline 必须与当前 render pass 兼容；
4. draw call 才能把 fragment output 写入 framebuffer attachments。

当前项目中：

```cpp
cmdBuf_.beginRenderPass(renderPassBegin, {});
cmdBuf_.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->pipeline);
cmdBuf_.draw(3, 1, 0, 0);
cmdBuf_.endRenderPass();
```

Pipeline 决定“如何画”，framebuffer 决定“画到哪张图像上”。

## 3.5 Framebuffer 与 Depth Attachment

当前项目的 framebuffer 只有一个 color image view。  
如果后续加入深度测试，framebuffer 需要同时绑定 color view 和 depth view。

RenderPass 中通常会有两个 attachment：

```text
attachment 0: color
attachment 1: depth
```

Framebuffer 创建时要提供两个 image views：

```cpp
std::array attachments = {
    swapchainColorView,
    depthImageView
};

vk::FramebufferCreateInfo createInfo;
createInfo.setRenderPass(renderPass)
          .setAttachments(attachments)
          .setWidth(extent.width)
          .setHeight(extent.height)
          .setLayers(1);
```

注意 depth image 通常不是 swapchain image，而是应用自己创建的 `vk::Image`。  
但它的尺寸一般要和 swapchain extent 一致，所以 swapchain resize 时 depth image、depth image view、framebuffer 都要重建。

# 4 生命周期与重建

## 4.1 生命周期

`vk::Framebuffer` 是 device 级对象，由 `vk::Device` 创建和销毁：

```cpp
vk::Framebuffer framebuffer =
    device.createFramebuffer(createInfo);

device.destroyFramebuffer(framebuffer);
```

Framebuffer 依赖 image view 和 render pass。  
因此销毁顺序通常是：

```text
先销毁 framebuffer
再销毁 image view
再销毁 swapchain image 所属的 swapchain
```

当前项目中 `Swapchain::~Swapchain()` 的顺序是：

```cpp
for (auto& framebuffer : framebuffers) {
    device.destroyFramebuffer(framebuffer);
}

for (auto& view : imageViews) {
    device.destroyImageView(view);
}

device.destroySwapchainKHR(swapchain);
```

这个顺序是合理的。  
因为 framebuffer 引用 image view，image view 引用 swapchain image，swapchain 管理 swapchain images。

销毁前还要保证 GPU 不再使用这些 framebuffer。  
当前项目在退出时先调用：

```cpp
ContextInstance.device.waitIdle();
```

这可以避免 command buffer 仍在使用 framebuffer 时就销毁资源。

## 4.2 何时需要重建 Framebuffer

以下情况通常需要重建 framebuffer：

1. swapchain 重建；
2. swapchain image view 重建；
3. framebuffer width / height 改变；
4. render pass attachment 数量或格式改变；
5. 加入或移除 depth attachment；
6. MSAA attachment 或 resolve attachment 改变；
7. array layer 数量改变。

窗口 resize 是最常见触发原因。  
即使 render pass 可以复用，framebuffer 也通常必须重建，因为它绑定的 swapchain image views 和尺寸已经变化。

重建时不要只创建新 framebuffer 而忘记销毁旧 framebuffer。  
否则会造成资源泄漏，也可能在旧资源仍被 GPU 使用时引发同步问题。

## 4.3 实践建议

1. RenderPass 先定义 attachment 规则，Framebuffer 再绑定具体 image views。
2. Framebuffer attachments 的数量和顺序必须与 render pass attachment 引用匹配。
3. 普通 swapchain 渲染通常每个 swapchain image view 创建一个 framebuffer。
4. 每帧使用 `acquireNextImageKHR` 返回的 `imageIndex` 选择 framebuffer。
5. present 使用的 `imageIndex` 应与 begin render pass 使用的 framebuffer index 保持一致。
6. Framebuffer 尺寸通常使用 swapchain extent，不要和窗口逻辑尺寸混淆。
7. 加入 depth attachment 后，framebuffer attachments 数组要同时包含 color view 和 depth view。
8. Swapchain 重建后，旧 framebuffer 必须重建。
9. 销毁顺序应是 framebuffer 早于 image view，image view 早于 swapchain。
10. 销毁或重建前确保 GPU 不再使用旧 framebuffer，简单做法是 `device.waitIdle()`。

## 4.4 当前项目的 Framebuffer 链路

Toy2D 当前 framebuffer 链路可以概括为：

```text
Swapchain::getImages
    -> Swapchain::createImageViews
    -> RenderProcess::InitRenderPass
    -> Swapchain::CreateFramebuffers
    -> Renderer::Render
    -> acquireNextImageKHR
    -> renderPassBegin.setFramebuffer(framebuffers[imageIndex])
    -> beginRenderPass
    -> bindPipeline + draw
    -> endRenderPass
    -> presentKHR(imageIndex)
```

这条链路说明了 framebuffer 的位置：  
它位于 image view 之后、render pass 执行之前，是把“抽象 attachment 规则”落到“具体 swapchain image”上的关键对象。
