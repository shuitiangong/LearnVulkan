# 1 CommandBuffer

## 1.1 概念

`vk::CommandBuffer`（C API: `VkCommandBuffer`）是 Vulkan 中记录 GPU 命令的对象。  
应用不会像传统即时模式 API 那样直接让 GPU 立刻执行每个 draw call，而是先把命令录制进 command buffer，再把 command buffer 提交到 queue，由 GPU 异步执行。

可以把 command buffer 理解成一份“GPU 工作清单”：

```text
begin command buffer
    -> begin render pass
    -> bind pipeline
    -> draw
    -> end render pass
end command buffer
    -> queue submit
```

CommandBuffer 的核心职责：

1. 记录渲染命令、拷贝命令、pipeline barrier 等 GPU 命令；
2. 把一组命令组织成可提交给 queue 的批次；
3. 减少 CPU 与 GPU 之间的即时交互；
4. 支持多线程录制命令；
5. 让命令提交和同步控制更加显式。

CommandBuffer 本身不执行命令。  
真正执行发生在：

```cpp
graphicsQueue.submit(submitInfo, fence);
```

也就是说，`cmdBuf.draw(...)` 只是把 draw 命令写入 command buffer；GPU 只有在 queue submit 之后才会执行这些命令。

CommandBuffer 通常从 `vk::CommandPool` 分配。  
CommandPool 和 queue family 绑定，因为 command buffer 最终要提交到某个 queue family 的 queue 上执行。

当前项目中 `Renderer` 保存了：

```cpp
vk::CommandPool cmdPool_;
vk::CommandBuffer cmdBuf_;
```

使用的是一个 primary command buffer，每帧 reset 后重新录制。  
这种方式适合入门阶段：结构简单、容易理解，但并发能力和多帧并行能力有限。

常见误区：

1. **以为调用 draw 就立刻执行**：draw 只是录制命令，submit 后 GPU 才执行。
2. **CommandBuffer 可以随便重用**：重用前必须确保 GPU 不再使用它，并且 command pool / command buffer 支持 reset。
3. **忘记 end command buffer**：必须 `begin -> record -> end` 后才能 submit。
4. **CommandPool 与 queue family 无关**：实际上 command pool 创建时必须指定 queue family index。
5. **在 render pass 外调用 graphics draw**：传统 render pass 路线中，绘制命令通常要在 `beginRenderPass` 和 `endRenderPass` 之间。

## 1.2 创建流程

### 1.2.1 创建 CommandPool：`vk::Device::createCommandPool`

目标：创建用于分配 command buffer 的池。  
CommandPool 必须指定一个 queue family index，表示从这个 pool 分配出的 command buffer 主要提交到哪个 queue family。

当前项目代码：

```cpp
void Renderer::initCmdPool() {
    vk::CommandPoolCreateInfo createInfo;
    createInfo.setQueueFamilyIndex(
                  Context::GetInstance().queueFamilyIndices.graphicsQueue.value())
              .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    cmdPool_ = Context::GetInstance()
        .device
        .createCommandPool(createInfo);
}
```

关键参数：

1. `setQueueFamilyIndex`：指定 command buffer 目标 queue family；
2. `eResetCommandBuffer`：允许单独 reset 从该 pool 分配出的 command buffer；
3. `eTransient`：提示 command buffer 生命周期较短，驱动可做相应优化。

这里要特别注意：`CommandPool` 不是“默认就和 graphics queue 对应”的。  
如果没有显式写出 `queueFamilyIndex`，代码可能只是碰巧依赖了某个默认值，例如 `0`，而这只有在当前设备上 graphics family 恰好也是 `0` 时才不会出问题。  
更稳妥的做法始终是像当前代码这样，显式绑定到 `graphicsQueue` 对应的 family index。

当前项目设置了 `eResetCommandBuffer`，所以后续可以每帧调用：

```cpp
cmdBuf_.reset();
```

如果没有这个 flag，就不能随意单独 reset command buffer，通常要 reset 整个 command pool。

---

### 1.2.2 分配 CommandBuffer：`vk::Device::allocateCommandBuffers`

目标：从 command pool 中分配一个或多个 command buffer。  
当前项目分配一个 primary command buffer：

```cpp
void Renderer::allocCmdBuf() {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(cmdPool_)
             .setCommandBufferCount(1)
             .setLevel(vk::CommandBufferLevel::ePrimary);

    cmdBuf_ = Context::GetInstance()
        .device
        .allocateCommandBuffers(allocInfo)[0];
}
```

CommandBuffer level 有两类：

1. `vk::CommandBufferLevel::ePrimary`：可以直接提交到 queue；
2. `vk::CommandBufferLevel::eSecondary`：不能直接 submit，通常在 primary command buffer 内执行。

当前项目使用 primary command buffer 是最常见的入门方式。  
Secondary command buffer 更适合大型渲染器中做多线程命令录制，例如每个线程录制一批物体的绘制命令，再由 primary command buffer 统一执行。

---

### 1.2.3 开始录制：`vk::CommandBuffer::begin`

目标：进入 command buffer 录制状态。  
当前项目每帧重置并重新录制：

```cpp
cmdBuf_.reset();

vk::CommandBufferBeginInfo begin;
begin.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

cmdBuf_.begin(begin);
```

`eOneTimeSubmit` 表示这个 command buffer 录制后只提交一次。  
当前项目每帧重新录制并提交一次，所以这个 flag 合理。

常见 usage flags：

1. `eOneTimeSubmit`：录制后提交一次；
2. `eRenderPassContinue`：secondary command buffer 在 render pass 内继续执行；
3. `eSimultaneousUse`：允许 command buffer 在仍处于 pending 状态时再次提交，但会带来额外约束和开销。

入门阶段通常避免使用 `eSimultaneousUse`，而是通过 fence 确认 GPU 完成后再重用 command buffer。

---

### 1.2.4 录制 RenderPass 与 Draw 命令

目标：把本帧渲染命令写入 command buffer。  
当前项目录制流程是：

```cpp
vk::RenderPassBeginInfo renderPassBegin;
renderPassBegin.setRenderPass(renderProcess->renderPass)
               .setRenderArea(area)
               .setFramebuffer(swapchain->framebuffers[imageIndex])
               .setClearValues(clearValue);

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

这段命令的含义：

1. `beginRenderPass`：指定本帧使用的 render pass 和 framebuffer；
2. `bindPipeline`：绑定 graphics pipeline；
3. `draw(3, 1, 0, 0)`：绘制 3 个顶点、1 个实例；
4. `endRenderPass`：结束 render pass，触发 attachment store 和 final layout transition。

这里的 `imageIndex` 来自 swapchain acquire。  
它决定本帧写入哪个 framebuffer，也间接决定写入哪张 swapchain image。

---

### 1.2.5 结束录制：`vk::CommandBuffer::end`

目标：结束 command buffer 录制，使其进入可提交状态。

```cpp
cmdBuf_.end();
```

只有成功 `end` 之后，command buffer 才能提交到 queue。  
如果录制过程中出现状态错误，例如在 render pass 外调用了必须在 render pass 内执行的命令，验证层通常会在录制或提交阶段报告错误。

---

### 1.2.6 提交 CommandBuffer：`vk::Queue::submit`

目标：把录制好的 command buffer 提交到 graphics queue，让 GPU 执行。  
提交时通常还会指定 semaphore 和 fence。

当前项目：

```cpp
vk::SubmitInfo submit;
constexpr vk::PipelineStageFlags waitDstStage =
    vk::PipelineStageFlagBits::eColorAttachmentOutput;

submit.setCommandBuffers(cmdBuf_)
      .setWaitSemaphores(imageAvaliable_)
      .setWaitDstStageMask(waitDstStage)
      .setSignalSemaphores(imageDrawFinish_[imageIndex]);

Context::GetInstance()
    .graphicsQueue
    .submit(submit, cmdAvaliableFence_);
```

这次提交表达了三个同步关系：

1. graphics queue 要等待 `imageAvaliable_`；
2. 等待发生在 `eColorAttachmentOutput` 阶段；
3. command buffer 执行完成后 signal `imageDrawFinish_[imageIndex]`；
4. 整个 submit 完成后 signal `cmdAvaliableFence_`，让 CPU 可以等待。

CommandBuffer 与同步对象的关系可以概括为：

```text
Semaphore:
    控制 GPU queue 与 GPU queue 之间的顺序

Fence:
    控制 CPU 是否等待 GPU 完成某次 submit
```

# 2 Semaphore

## 2.1 概念

`vk::Semaphore`（C API: `VkSemaphore`）是 GPU 侧同步对象。  
它主要用于 queue 之间或 queue 操作之间的依赖关系，例如：

1. swapchain image 可用后，graphics queue 才能开始写入；
2. graphics queue 渲染完成后，present queue 才能提交显示；
3. transfer queue 拷贝完成后，graphics queue 才能读取资源。

Semaphore 的核心特点：

1. 主要用于 GPU-GPU 同步；
2. CPU 不能像等待 fence 那样直接 `waitSemaphore` 等待普通 binary semaphore；
3. 常见用法是在 queue submit / present 中作为 wait 或 signal 对象；
4. binary semaphore 一次 signal 对应一次 wait；
5. Vulkan 也支持 timeline semaphore，但当前项目使用的是 binary semaphore。

当前项目中有两类 semaphore：

```cpp
vk::Semaphore imageAvaliable_;
std::vector<vk::Semaphore> imageDrawFinish_;
```

含义分别是：

1. `imageAvaliable_`：swapchain image 已经 acquire，可以开始渲染；
2. `imageDrawFinish_[imageIndex]`：对应 swapchain image 的绘制已经完成，可以 present。

变量名 `imageAvaliable_` 是项目中的拼写，语义上可理解为 `imageAvailable_`。

常见误区：

1. **用 semaphore 做 CPU 等待**：CPU 等待 GPU 完成应使用 fence。
2. **signal 后没有 wait**：binary semaphore signal 后必须有对应 wait，否则不能安全复用。
3. **过早复用 semaphore**：必须确认之前的 signal/wait 生命周期已经结束。
4. **把 acquire semaphore 和 render-finished semaphore 混用**：它们表达的是不同阶段的依赖。

## 2.2 创建流程

### 2.2.1 创建 Semaphore：`vk::Device::createSemaphore`

目标：创建用于 GPU 队列同步的 semaphore。  
当前项目创建一个 image-available semaphore，并为每张 swapchain image 创建一个 render-finished semaphore：

```cpp
void Renderer::createSems() {
    vk::SemaphoreCreateInfo createInfo;

    imageAvaliable_ =
        Context::GetInstance().device.createSemaphore(createInfo);

    imageDrawFinish_.resize(
        Context::GetInstance().swapchain->images.size()
    );

    for (auto& sem : imageDrawFinish_) {
        sem = Context::GetInstance().device.createSemaphore(createInfo);
    }
}
```

`vk::SemaphoreCreateInfo` 对 binary semaphore 来说通常不需要额外参数。  
如果使用 timeline semaphore，则需要通过 `pNext` 链接 `vk::SemaphoreTypeCreateInfo`，当前项目暂时不涉及。

---

### 2.2.2 Acquire 时 Signal：`vk::Device::acquireNextImageKHR`

目标：从 swapchain 获取一张可渲染图像，并在图像可用时 signal semaphore。

当前项目：

```cpp
auto result = device.acquireNextImageKHR(
    swapchain->swapchain,
    std::numeric_limits<uint64_t>::max(),
    imageAvaliable_
);

auto imageIndex = result.value;
```

这里的 `imageAvaliable_` 表示：当 swapchain image 真正可供渲染时，该 semaphore 被 signal。  
随后 graphics queue submit 会等待它：

```cpp
submit.setWaitSemaphores(imageAvaliable_);
```

这防止 GPU 在图像还没有从 presentation engine 释放出来时就开始写入。

---

### 2.2.3 Submit 时 Wait：`vk::SubmitInfo::setWaitSemaphores`

目标：让 graphics queue 等待 swapchain image 可用后再执行绘制命令。

```cpp
constexpr vk::PipelineStageFlags waitDstStage =
    vk::PipelineStageFlagBits::eColorAttachmentOutput;

submit.setWaitSemaphores(imageAvaliable_)
      .setWaitDstStageMask(waitDstStage);
```

`waitDstStageMask` 很重要。  
它表示 command buffer 执行到哪个 pipeline stage 之前必须等待 semaphore。

对于 swapchain color attachment 渲染，通常使用：

```cpp
vk::PipelineStageFlagBits::eColorAttachmentOutput
```

含义是：在真正写 color attachment 之前，必须等到 swapchain image 可用。  
这比在 `eTopOfPipe` 过早等待更精确。

---

### 2.2.4 Submit 时 Signal：`vk::SubmitInfo::setSignalSemaphores`

目标：让 graphics queue 在绘制命令完成后 signal 一个 semaphore，通知后续 present 可以等待。

当前项目：

```cpp
submit.setSignalSemaphores(imageDrawFinish_[imageIndex]);
```

这个 semaphore 表示“对应 imageIndex 的渲染已经完成”。  
present queue 后续会等待它：

```cpp
present.setWaitSemaphores(imageDrawFinish_[imageIndex]);
```

这防止 presentation engine 在 GPU 尚未完成颜色附件写入时就读取 swapchain image。

---

### 2.2.5 Present 时 Wait：`vk::PresentInfoKHR::setWaitSemaphores`

目标：让 present 等待 graphics queue 渲染完成后再显示图像。

```cpp
vk::PresentInfoKHR present;
present.setImageIndices(imageIndex)
       .setSwapchains(swapchain->swapchain)
       .setWaitSemaphores(imageDrawFinish_[imageIndex]);

Context::GetInstance().presentQueue.presentKHR(present);
```

这里建立了第二段 GPU 同步：

```text
graphics queue 渲染完成
    -> signal imageDrawFinish_[imageIndex]
    -> present queue wait
    -> present imageIndex
```

因此，一帧中的 semaphore 链路是：

```text
acquireNextImageKHR
    -> signal imageAvaliable_
    -> graphics submit wait imageAvaliable_
    -> graphics submit signal imageDrawFinish_[imageIndex]
    -> present wait imageDrawFinish_[imageIndex]
```

# 3 Fence

## 3.1 概念

`vk::Fence`（C API: `VkFence`）是 CPU 与 GPU 之间的同步对象。  
它通常用于 CPU 等待某次 queue submit 完成，避免 CPU 过早修改、重置或销毁 GPU 仍在使用的资源。

Fence 的核心特点：

1. 由 queue submit 在 GPU 完成后 signal；
2. CPU 可以通过 `waitForFences` 等待它；
3. signal 后需要 `resetFences` 才能再次用于新的 submit；
4. 常用于控制 frames in flight、command buffer 重用、资源销毁和 swapchain 重建。

Semaphore 和 Fence 的区别：

```text
Semaphore:
    GPU 等 GPU
    用在 queue submit / present 之间

Fence:
    CPU 等 GPU
    用在 CPU 侧等待某次 submit 完成
```

当前项目中有一个 fence：

```cpp
vk::Fence cmdAvaliableFence_;
```

语义上表示：本次 command buffer submit 是否已经完成。  
项目每帧 submit 后等待 fence，然后 reset fence，保证下一帧重用同一个 command buffer 前 GPU 已完成上一帧工作。

常见误区：

1. **提交前忘记 reset fence**：一个已 signal 的 fence 如果不 reset，再次 wait 会立即返回，失去同步意义。
2. **在没有 submit 的情况下等待 unsignaled fence**：会导致 CPU 一直等待。
3. **把 fence 当作 GPU-GPU 同步对象**：GPU queue 之间应使用 semaphore 或 pipeline barrier。
4. **过度使用 `device.waitIdle()`**：它会等待整个 device 空闲，简单但粗暴；每帧同步更常用 per-frame fence。

## 3.2 创建与使用流程

### 3.2.1 创建 Fence：`vk::Device::createFence`

目标：创建 CPU 可等待的同步对象。  
当前项目：

```cpp
void Renderer::createFence() {
    vk::FenceCreateInfo createInfo;
    cmdAvaliableFence_ =
        Context::GetInstance().device.createFence(createInfo);
}
```

默认创建的是 unsignaled fence。  
当前项目是在每帧 submit 后等待它，因此第一帧不会在 submit 前等待，默认 unsignaled 是可行的。

如果采用“每帧开头先 wait fence，再 reset fence，再 submit”的常见 frames-in-flight 模式，通常会把 fence 创建为 signaled：

```cpp
vk::FenceCreateInfo createInfo;
createInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

vk::Fence inFlightFence = device.createFence(createInfo);
```

这样第一帧开头等待 fence 时不会卡死。

---

### 3.2.2 Submit 时绑定 Fence：`vk::Queue::submit`

目标：让 fence 在本次 submit 的所有命令执行完成后被 signal。

当前项目：

```cpp
Context::GetInstance()
    .graphicsQueue
    .submit(submit, cmdAvaliableFence_);
```

当 graphics queue 完成这次 submit 中的 command buffer 后，`cmdAvaliableFence_` 会变成 signaled。  
CPU 随后可以等待它。

Fence signal 的范围是这次 submit，而不是 present 完成。  
当前项目中的 fence 等待的是 graphics submit 完成；present queue 的显示完成由 swapchain acquire/present 机制和 semaphore 协调。

---

### 3.2.3 CPU 等待 Fence：`vk::Device::waitForFences`

目标：让 CPU 等待 GPU 完成本次提交，确保可以安全重用 command buffer 或相关资源。

当前项目：

```cpp
if (Context::GetInstance().device.waitForFences(
        cmdAvaliableFence_,
        true,
        std::numeric_limits<uint64_t>::max()
    ) != vk::Result::eSuccess) {
    std::cout << "waitForFences failed\n";
}
```

参数说明：

1. 第一个参数：等待的 fence；
2. 第二个参数 `true`：等待全部 fences signal；
3. 第三个参数：超时时间，`uint64_t::max()` 表示几乎无限等待。

当前项目在每帧末尾等待 fence，因此 CPU 会等 GPU 完成本帧绘制后再进入下一帧。  
这种方式简单，但会降低 CPU/GPU 并行度。

更常见的实时渲染做法是允许 2 或 3 帧同时在飞行（frames in flight），每帧有独立 command buffer、semaphore、fence。这样 CPU 可以准备下一帧，而 GPU 仍在执行上一帧。

---

### 3.2.4 重置 Fence：`vk::Device::resetFences`

目标：把已经 signaled 的 fence 重新变成 unsignaled，以便下一次 submit 使用。

当前项目：

```cpp
Context::GetInstance()
    .device
    .resetFences(cmdAvaliableFence_);
```

Fence 的典型生命周期：

```text
unsignaled
    -> queue submit
    -> GPU 完成 submit
    -> signaled
    -> CPU wait 成功
    -> reset
    -> unsignaled
```

注意：不要在 fence 仍被某个未完成 submit 使用时 reset。  
通常做法是 wait 成功后再 reset，或者在下一次 submit 前确认该 fence 已经不再 pending。

# 4 一帧渲染中的同步链路

## 4.1 概念

CommandBuffer、Semaphore、Fence 在一帧中分别解决不同问题：

1. CommandBuffer：记录 GPU 要执行什么；
2. Semaphore：控制 GPU 操作之间的先后顺序；
3. Fence：让 CPU 知道 GPU 是否完成了某次提交。

当前项目的一帧渲染链路：

```text
CPU:
    acquireNextImageKHR
        -> 得到 imageIndex
    reset command buffer
    record command buffer
    submit command buffer
    present
    wait fence
    reset fence

GPU:
    acquire signal imageAvaliable_
    graphics queue wait imageAvaliable_
    execute command buffer
    graphics queue signal imageDrawFinish_[imageIndex]
    present queue wait imageDrawFinish_[imageIndex]
    present image
    graphics submit signal fence
```

这条链路中，最关键的是两个 semaphore 和一个 fence：

```text
imageAvaliable_:
    swapchain image 可用 -> graphics queue 可写入

imageDrawFinish_[imageIndex]:
    graphics queue 写完 -> present queue 可显示

cmdAvaliableFence_:
    graphics submit 完成 -> CPU 可继续安全重用 command buffer
```

## 4.2 同步等待的底层原理

### 4.2.1 为什么 API 看起来是同步的

目标：区分 **CPU 按顺序调用 API** 和 **GPU 真正完成执行** 这两件事。  
当前项目中，一帧渲染的 C++ 代码是顺序往下执行的：

```cpp
auto result = device.acquireNextImageKHR(..., imageAvaliable_);
cmdBuf_.begin(begin);
cmdBuf_.draw(3, 1, 0, 0);
graphicsQueue.submit(submit, cmdAvaliableFence_);
presentQueue.presentKHR(present);
```

从 CPU 视角看，这当然是同步的：上一行执行完，下一行才会开始。  
但这里的“同步”只说明 **CPU 线程正在顺序调用函数**，并不说明：

1. swapchain image 已经可以立即安全写入；
2. `draw` 代表的 GPU 工作已经立刻执行完；
3. `submit` 返回时 command buffer 已经执行完成；
4. `presentKHR` 调用时图像内容已经完成写入。

Vulkan 的执行模型本质上是：

```text
CPU 顺序提交工作
!=
GPU 顺序完成工作
```

也就是说，很多 API 返回时，GPU 仍然在后台异步执行。  
如果不显式建立依赖关系，后续步骤就可能在错误的时机读取或覆盖资源。

---

### 4.2.2 为什么“代码顺序”不等于“执行顺序”

目标：理解为什么仅靠 C++ 调用顺序不足以保证 GPU 正确工作。  
例如当前项目中：

```cpp
graphicsQueue.submit(submit, cmdAvaliableFence_);
presentQueue.presentKHR(present);
```

CPU 一定是先调用 `submit`，再调用 `presentKHR`。  
但这只能说明：

1. CPU 先把“渲染任务”交给 graphics queue；
2. CPU 再把“显示任务”交给 present queue。

它不能自动保证：

1. present 一定发生在渲染完成之后；
2. present 读取的 swapchain image 一定已经写完；
3. CPU 在下一帧重置 command buffer 时，GPU 已经停止使用它。

原因是 graphics queue、present queue、presentation engine 都是异步推进的。  
因此 Vulkan 不把“调用顺序”当成“资源依赖关系”，而是要求应用通过同步原语显式声明：

```text
谁必须等谁
谁完成后才能放行谁
谁的结果何时对谁可见
```

---

### 4.2.3 Semaphore 为什么能等待

目标：理解 semaphore 为什么能让 GPU 等待 GPU。  
Semaphore 本质上不是普通 C++ 变量，而是 Vulkan 执行模型中的正式同步原语。  
当应用这样写：

```cpp
submit.setWaitSemaphores(imageAvaliable_)
      .setSignalSemaphores(imageDrawFinish_[imageIndex]);
```

以及：

```cpp
present.setWaitSemaphores(imageDrawFinish_[imageIndex]);
```

它表达的不是“CPU 在这里帮你等一等”，而是把同步关系写进了提交描述中：

1. graphics queue 在执行这次 submit 前，要等待 `imageAvaliable_` 被 signal；
2. graphics queue 完成这次 submit 后，要 signal `imageDrawFinish_[imageIndex]`；
3. present 在真正开始读取 swapchain image 前，要等待 `imageDrawFinish_[imageIndex]`。

驱动在收到这些提交信息后，会把 semaphore 依赖一并交给底层调度系统。  
从抽象层面可以理解为：

```text
应用线程
    -> Vulkan 驱动
    -> GPU 队列调度
    -> GPU 执行单元 / 呈现引擎
```

在这条链路里，semaphore 被识别为“前后任务之间的放行条件”。  
signal 之前，后续工作不会继续；signal 之后，依赖它的工作才会被调度或继续执行。

所以 semaphore 的本质不是“一个可以手动等待的变量”，而是：

> **被驱动和 GPU 调度系统识别的 GPU-GPU 依赖信号。**

---

### 4.2.4 Fence 为什么能等待

目标：理解 fence 为什么能让 CPU 等待 GPU。  
Fence 也是正式同步原语，但它面向的是 CPU。  
当前项目中：

```cpp
graphicsQueue.submit(submit, cmdAvaliableFence_);
```

这表示：

> 当这次 submit 中的 GPU 工作全部完成后，把 `cmdAvaliableFence_` 置为 signaled。

随后 CPU 调用：

```cpp
device.waitForFences(
    cmdAvaliableFence_,
    true,
    std::numeric_limits<uint64_t>::max()
);
```

本质上是在询问：

```text
这次 submit 对应的 GPU 工作真的做完了吗？
如果没做完，CPU 就继续等待。
```

底层可以粗略理解为：

1. submit 把 fence 绑定到这次 GPU 提交；
2. GPU 完成这次提交后，驱动/内核会更新 fence 状态；
3. `waitForFences` 观察 fence 状态；
4. fence 从 unsignaled 变成 signaled 后，CPU 等待结束。

所以 fence 的本质是：

> **给 CPU 看的 GPU 完成标记。**

和 semaphore 不同，fence 不是给另一个 GPU 队列“放行”的，而是给 CPU 一个“现在可以安全继续了”的信号。

---

### 4.2.5 结合当前项目看三段等待

目标：把同步原理落回当前项目的一帧流程。  
当前项目里有三段最关键的等待关系：

1. **acquire -> render**
   - `acquireNextImageKHR(..., imageAvaliable_)`
   - `submit.waitSemaphores(imageAvaliable_)`
   - 含义：swapchain image 真正可写后，graphics queue 才能开始写 color attachment。

2. **render -> present**
   - `submit.signalSemaphores(imageDrawFinish_[imageIndex])`
   - `present.waitSemaphores(imageDrawFinish_[imageIndex])`
   - 含义：渲染完成后，present 才能读取该 swapchain image。

3. **submit 完成 -> CPU 重用资源**
   - `graphicsQueue.submit(submit, cmdAvaliableFence_)`
   - `waitForFences(cmdAvaliableFence_, ...)`
   - 含义：GPU 完成上一帧提交后，CPU 才能安全 reset command buffer 和 fence。

这三段关系如果只靠“代码顺序”是无法自动保证的。  
Vulkan 之所以需要 semaphore 和 fence，就是因为它把这类依赖从“驱动替应用猜”变成了“应用显式声明”。

因此，同步等待的底层原理可以概括成一句话：

> **CPU 顺序调用 API 只是在提交工作；真正的等待与放行，是通过驱动和 GPU 调度系统识别 semaphore / fence 后建立起来的显式执行依赖。**

## 4.3 当前项目流程

### 4.3.1 获取图像并等待可用：`acquireNextImageKHR`

目标：获取本帧要渲染的 swapchain image，并准备 GPU-GPU 同步。

```cpp
auto result = device.acquireNextImageKHR(
    swapchain->swapchain,
    std::numeric_limits<uint64_t>::max(),
    imageAvaliable_
);

auto imageIndex = result.value;
```

`imageAvaliable_` 被 signal 后，graphics queue 才能安全写入该 swapchain image。

---

### 4.3.2 录制命令：`begin` / `beginRenderPass` / `draw` / `end`

目标：把本帧 GPU 命令写入 command buffer。

```cpp
cmdBuf_.reset();

vk::CommandBufferBeginInfo begin;
begin.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
cmdBuf_.begin(begin);

cmdBuf_.beginRenderPass(renderPassBegin, {});
cmdBuf_.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->pipeline);
cmdBuf_.draw(3, 1, 0, 0);
cmdBuf_.endRenderPass();

cmdBuf_.end();
```

CommandBuffer 记录的是“要做什么”。  
它本身不保证资源可用，也不保证执行完成；这些由 submit、semaphore、fence 处理。

---

### 4.3.3 提交绘制：`vk::Queue::submit`

目标：提交 command buffer，并建立 acquire -> render -> present 的前半段同步。

```cpp
vk::SubmitInfo submit;
constexpr vk::PipelineStageFlags waitDstStage =
    vk::PipelineStageFlagBits::eColorAttachmentOutput;

submit.setCommandBuffers(cmdBuf_)
      .setWaitSemaphores(imageAvaliable_)
      .setWaitDstStageMask(waitDstStage)
      .setSignalSemaphores(imageDrawFinish_[imageIndex]);

Context::GetInstance().graphicsQueue.submit(
    submit,
    cmdAvaliableFence_
);
```

这段提交表示：

1. 等待 image available；
2. 执行 command buffer；
3. 渲染完成后 signal render finished semaphore；
4. submit 全部完成后 signal fence。

---

### 4.3.4 提交显示：`vk::Queue::presentKHR`

目标：等待渲染完成，然后把同一个 imageIndex 提交给窗口系统显示。

```cpp
vk::PresentInfoKHR present;
present.setImageIndices(imageIndex)
       .setSwapchains(swapchain->swapchain)
       .setWaitSemaphores(imageDrawFinish_[imageIndex]);

Context::GetInstance().presentQueue.presentKHR(present);
```

`present` 等待的是 `imageDrawFinish_[imageIndex]`，因此它不会在渲染完成前读取 swapchain image。  
这就是 graphics queue 与 present queue 之间最重要的同步。

---

### 4.3.5 CPU 等待 GPU 完成：`waitForFences`

目标：确保本次 graphics submit 完成后，再重用 command buffer 和 fence。

```cpp
device.waitForFences(
    cmdAvaliableFence_,
    true,
    std::numeric_limits<uint64_t>::max()
);

device.resetFences(cmdAvaliableFence_);
```

当前项目每帧都等待，流程直观但比较保守。  
后续如果要提高性能，可以改成多帧并行结构：

```text
Frame 0: command buffer 0 + semaphores 0 + fence 0
Frame 1: command buffer 1 + semaphores 1 + fence 1
Frame 2: command buffer 2 + semaphores 2 + fence 2
```

每帧开头等待当前 frame slot 的 fence，避免覆盖 GPU 仍在使用的资源。

# 5 生命周期与实践建议

## 5.1 销毁顺序

CommandPool、Semaphore、Fence 都是 device 级对象。  
销毁时必须保证 device 仍然有效，并且 GPU 不再使用它们。

当前项目 `Renderer::~Renderer()`：

```cpp
auto& device = Context::GetInstance().device;

device.freeCommandBuffers(cmdPool_, cmdBuf_);
device.destroyCommandPool(cmdPool_);

device.destroySemaphore(imageAvaliable_);
for (const auto& sem : imageDrawFinish_) {
    device.destroySemaphore(sem);
}

device.destroyFence(cmdAvaliableFence_);
```

销毁前，项目在 `toy2d::Quit()` 中调用：

```cpp
ContextInstance.device.waitIdle();
```

这很重要。  
如果 GPU 仍在执行使用 command buffer、semaphore 或 fence 的提交，就销毁这些对象，会导致验证层错误或未定义行为。

## 5.2 CommandBuffer 实践建议

1. CommandBuffer 从 CommandPool 分配，CommandPool 应指定正确的 queue family index。
2. 录制顺序必须是 `begin -> record -> end`。
3. draw call 只是录制命令，submit 后 GPU 才执行。
4. 重用 command buffer 前，要确认 GPU 不再使用它。
5. 每帧重新录制时，使用 `eResetCommandBuffer` 可以单独 reset command buffer。
6. 入门阶段用一个 primary command buffer 足够，复杂场景再考虑 secondary command buffer。
7. swapchain framebuffer 改变后，引用旧 framebuffer 的 command buffer 必须重新录制。

## 5.3 Semaphore 实践建议

1. Semaphore 用于 GPU-GPU 同步，不用于 CPU 等待。
2. `acquireNextImageKHR` 常 signal image-available semaphore。
3. graphics submit 常 wait image-available semaphore。
4. graphics submit 常 signal render-finished semaphore。
5. present 常 wait render-finished semaphore。
6. binary semaphore signal 后要有对应 wait，复用前要确认上一轮使用结束。
7. 多帧并行时，通常为每个 frame-in-flight 准备独立 semaphore。
8. 如果同步需求变复杂，可以学习 timeline semaphore，但入门阶段 binary semaphore 更直观。

## 5.4 Fence 实践建议

1. Fence 用于 CPU-GPU 同步，常用于等待某次 queue submit 完成。
2. Fence signal 后，复用前必须 reset。
3. 如果在每帧开头等待 fence，初始 fence 通常要创建为 signaled。
4. 如果在 submit 后立即等待 fence，逻辑简单但会降低 CPU/GPU 并行度。
5. 不要用 fence 替代 semaphore 做 queue 与 queue 之间的 GPU 同步。
6. 资源销毁、swapchain 重建前，可用 `device.waitIdle()` 简化同步，但正常逐帧渲染不宜过度使用。

## 5.5 当前项目的改进方向

当前 Toy2D 的同步结构适合学习：

1. 一个 command buffer；
2. 一个 image-available semaphore；
3. 每个 swapchain image 一个 render-finished semaphore；
4. 一个 fence；
5. 每帧等待 fence，保证串行安全。

后续可以逐步改进为更常见的 frames-in-flight 结构：

```text
MAX_FRAMES_IN_FLIGHT = 2

每个 frame slot:
    command buffer
    imageAvailable semaphore
    renderFinished semaphore
    inFlight fence
```

渲染循环通常变成：

```text
wait current frame fence
reset current frame fence
acquire image
record current frame command buffer
submit with current frame semaphores and fence
present
currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT
```

这样 CPU 可以在 GPU 执行上一帧时准备下一帧，吞吐更好。  
但它也要求更精细地管理 command buffer、per-frame resource、swapchain image 与 fence 的对应关系。
