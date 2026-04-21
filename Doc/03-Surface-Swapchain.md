# 1 Surface

## 1.1 概念

`vk::SurfaceKHR`（C API: `VkSurfaceKHR`）是 Vulkan 与操作系统窗口系统之间的连接对象。  
它不代表窗口本身，也不保存真正的像素图像；它的职责是把“Vulkan 渲染结果最终要显示到哪个平台窗口”这件事抽象成一个统一接口。

Vulkan 本身是跨平台图形 API，但窗口系统并不跨平台：

1. Windows 使用 Win32 window handle；
2. Linux 可能使用 Xlib、XCB、Wayland；
3. macOS/iOS 通常通过 Metal layer 间接接入；
4. SDL/GLFW 这类库会在内部帮你适配不同平台。

如果没有 `surface`，Vulkan 仍然可以做离屏渲染、计算、图像处理，但它无法知道如何把最终图像交给某个窗口系统显示。因此在窗口渲染链路里，`surface` 是连接 Vulkan 与屏幕显示的第一层 WSI（Window System Integration）对象。

它在初始化链路中的位置通常是：

1. 创建窗口；
2. 查询窗口系统要求的 instance extensions；
3. 创建 `vk::Instance`；
4. 通过 SDL/GLFW 创建 `vk::SurfaceKHR`；
5. 用 `surface` 查询 present queue、surface format、present mode、surface capabilities；
6. 基于这些信息创建 `vk::SwapchainKHR`。

常见误区：

1. **把 surface 当成 framebuffer**：surface 只描述“显示目标”，真正可渲染的图像来自 swapchain images。
2. **忘记启用窗口系统实例扩展**：例如 Windows 下通常需要 `VK_KHR_surface` 和 `VK_KHR_win32_surface`，这些扩展必须在创建 instance 时启用。
3. **认为 graphics queue 一定能 present**：图形命令能力和呈现能力是两件事，present 支持必须针对 `surface` 单独查询。
4. **销毁顺序错误**：swapchain 依赖 surface，必须先销毁 swapchain，再销毁 surface。

## 1.2 创建流程

### 1.2.1 从窗口库获取实例扩展：`SDL_Vulkan_GetInstanceExtensions`

目标：在创建 `vk::Instance` 之前，拿到当前平台创建 surface 所需的 instance extensions。  
这些扩展不是固定写死的，因为不同平台所需扩展不同。项目中使用 SDL3，因此由 SDL 返回平台相关扩展。

```cpp
uint32_t extensionCount = 0;
const char* const* sdlExtensions =
    SDL_Vulkan_GetInstanceExtensions(&extensionCount);

if (!sdlExtensions || extensionCount == 0) {
    throw std::runtime_error(SDL_GetError());
}

std::vector<const char*> extensions(
    sdlExtensions,
    sdlExtensions + extensionCount
);
```

在 Windows 平台上，常见结果类似：

```text
VK_KHR_surface
VK_KHR_win32_surface
```

这一步必须发生在 `vk::createInstance` 之前，因为 instance 创建后不能再补加 instance extensions。

---

### 1.2.2 创建 Instance 时启用窗口扩展：`vk::InstanceCreateInfo::setPEnabledExtensionNames`

目标：把 SDL 返回的窗口系统扩展写入 instance 创建参数。  
如果项目还启用了调试工具扩展，可以额外追加 `VK_EXT_debug_utils`。

```cpp
std::vector<const char*> enabledExtensions = extensions;
enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

vk::InstanceCreateInfo createInfo;
createInfo.setPApplicationInfo(&appInfo)
          .setPEnabledLayerNames(layers)
          .setPEnabledExtensionNames(enabledExtensions);

vk::Instance instance = vk::createInstance(createInfo);
```

这里的关键点是：`VK_KHR_swapchain` 不属于 instance extension，而是 device extension。  
surface 相关平台扩展在 instance 阶段启用，swapchain 扩展在 logical device 阶段启用。

---

### 1.2.3 通过 SDL 创建 Surface：`SDL_Vulkan_CreateSurface`

目标：把 SDL window 转换成 Vulkan 可识别的 `VkSurfaceKHR`。  
在项目中，`Context::Init` 接收一个 `CreateSurfaceFunc`，由 `main.cpp` 把 SDL 创建 surface 的细节注入进来，这样 `Context` 不需要直接依赖 SDL window。

```cpp
toy2d::Init(extensions, [&](vk::Instance instance) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (!SDL_Vulkan_CreateSurface(
            window,
            static_cast<VkInstance>(instance),
            nullptr,
            &surface)) {
        throw std::runtime_error(
            std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError()
        );
    }

    return vk::SurfaceKHR(surface);
}, 1024, 720);
```

这段代码里有两个重要设计点：

1. SDL 负责平台差异，项目只保存 `vk::SurfaceKHR`；
2. surface 由 instance 创建和销毁，不由 logical device 创建。

---

### 1.2.4 查询队列族是否支持 Present：`vk::PhysicalDevice::getSurfaceSupportKHR`

目标：找到**能把图像提交到当前 surface 的 queue family**，也就是 present queue family。  
它有可能和 graphics queue family 是同一个，也有可能是另一个独立的 queue family。Present 能力依赖具体 surface，所以不能只看 `vk::QueueFamilyProperties::queueFlags`。

```cpp
auto properties = phyDevice.getQueueFamilyProperties();

for (std::size_t i = 0; i < properties.size(); ++i) {
    const auto& property = properties[i];

    if (property.queueFlags & vk::QueueFlagBits::eGraphics) {
        queueFamilyIndices.graphicsQueue = static_cast<uint32_t>(i);
    }

    if (phyDevice.getSurfaceSupportKHR(
            static_cast<uint32_t>(i),
            surface)) {
        queueFamilyIndices.presentQueue = static_cast<uint32_t>(i);
    }

    if (queueFamilyIndices) {
        break;
    }
}
```

这段代码同时记录 graphics family 和 present family，是因为当前项目后续创建 device 和 swapchain 时两者都需要。  
如果 graphics family 和 present family 是同一个，后续可以使用一个 queue 同时渲染和呈现。  
如果它们不同，就需要在创建 logical device 时同时申请两个 family 的 queue，并在 swapchain 创建时处理 image sharing mode。

## 1.3 生命周期与顺序要求

`surface` 的生命周期由 instance 管理，但它会被 physical device 查询、queue family 查询和 swapchain 创建共同使用。推荐顺序是：

1. 创建：`Instance -> Surface -> PhysicalDevice/QueueFamily 查询 -> Device -> Swapchain`；
2. 使用：`Surface` 作为查询和创建 swapchain 的输入，不直接参与 draw call；
3. 销毁：`Renderer/Framebuffer/ImageView/Swapchain -> Surface/Device -> Instance`，核心规则是 swapchain 必须早于 surface 销毁。

更准确地说，surface 至少要活到所有依赖它的 swapchain 都销毁之后。  
在项目里，`toy2d::Quit()` 会先 `device.waitIdle()`，再释放 renderer、render process、swapchain，最后释放 `Context`，这是避免 GPU 仍在使用 swapchain images 的关键。

# 2 Swapchain

## 2.1 概念

`vk::SwapchainKHR`（C API: `VkSwapchainKHR`）是 Vulkan 的窗口呈现图像队列。  
它管理一组可以被渲染、然后提交到窗口系统显示的图像，这些图像通常称为 swapchain images。

可以把 swapchain 理解成“GPU 渲染结果与屏幕刷新之间的缓冲队列”：

1. 应用从 swapchain 取得一张可写图像；
2. GPU 把当前帧渲染到这张图像；
3. 应用把这张图像 present 给窗口系统；
4. 窗口系统在合适的时间把它显示到屏幕上；
5. 图像重新回到可获取状态，供后续帧复用。

为什么需要多张图像？  
因为屏幕显示和 GPU 渲染不是同一个节奏。如果只有一张图像，显示系统正在读这张图像时，GPU 就不能安全地写它；如果 GPU 正在写，显示系统也不能安全地读它。swapchain 通过双缓冲、三缓冲等方式把“渲染”和“显示”解耦，减少等待。

swapchain 的核心参数包括：

1. **surface**：要显示到哪个窗口；
2. **image count**：swapchain 中有多少张图像；
3. **surface format**：图像格式和颜色空间；
4. **extent**：图像宽高，通常对应窗口 framebuffer 大小；
5. **present mode**：呈现策略，例如 FIFO、Mailbox；
6. **image usage**：图像用途，例如作为 color attachment；
7. **sharing mode**：graphics queue 和 present queue 是否跨 queue family 共享图像。

常见误区：

1. **以为 swapchain image 是自己创建的普通 image**：swapchain images 由 swapchain 创建，应用只获取句柄，不能手动销毁这些 images。
2. **忘记启用 `VK_KHR_swapchain`**：这是 device extension，必须在 `vk::DeviceCreateInfo` 中启用。
3. **窗口 resize 后继续使用旧 swapchain**：窗口尺寸、surface capabilities 或 present 状态变化后，通常需要重建 swapchain 及其 image views/framebuffers。
4. **把 image count 固定写死**：实际数量必须落在 `minImageCount` 和 `maxImageCount` 允许范围内；其中 `maxImageCount == 0` 表示没有显式上限。

## 2.2 创建前查询流程

### 2.2.1 启用设备扩展：`VK_KHR_SWAPCHAIN_EXTENSION_NAME`

目标：让 logical device 具备创建 swapchain 的能力。  
它不是 instance extension，而是 physical device 对应的 device extension。

```cpp
std::array deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

vk::DeviceCreateInfo createInfo;
createInfo.setQueueCreateInfos(queueCreateInfos)
          .setPEnabledExtensionNames(deviceExtensions);

vk::Device device = phyDevice.createDevice(createInfo);
```

更完整的设备筛选通常还会先检查扩展是否存在：

```cpp
auto extensions = phyDevice.enumerateDeviceExtensionProperties();

bool hasSwapchain = std::any_of(
    extensions.begin(),
    extensions.end(),
    [](const vk::ExtensionProperties& ext) {
        return std::strcmp(
            ext.extensionName,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        ) == 0;
    }
);
```

---

### 2.2.2 查询 Surface Format：`vk::PhysicalDevice::getSurfaceFormatsKHR`

目标：确定 swapchain image 的像素格式和颜色空间。  
常见选择是 SRGB 格式加 `vk::ColorSpaceKHR::eSrgbNonlinear`，这样更适合普通屏幕上的颜色显示。

```cpp
auto formats = phyDevice.getSurfaceFormatsKHR(surface);

vk::SurfaceFormatKHR selectedFormat = formats.front();
for (const auto& format : formats) {
    if (format.format == vk::Format::eR8G8B8A8Srgb &&
        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
        selectedFormat = format;
        break;
    }
}
```

---

### 2.2.3 查询 Surface Capabilities：`vk::PhysicalDevice::getSurfaceCapabilitiesKHR`

目标：获取当前 surface 对 swapchain 的限制，包括图像数量、尺寸范围、transform、usage 等。

```cpp
auto capabilities = phyDevice.getSurfaceCapabilitiesKHR(surface);
```

常用字段：

1. `minImageCount`：swapchain 至少需要多少张图像；
2. `maxImageCount`：允许的最大图像数，值为 `0` 时表示没有显式上限；
3. `currentExtent`：窗口系统指定的当前尺寸；
4. `minImageExtent / maxImageExtent`：应用可选择尺寸时的范围；
5. `currentTransform`：surface 当前 transform，通常需要写入 swapchain create info；
6. `supportedCompositeAlpha`：窗口 alpha 混合模式支持情况；
7. `supportedUsageFlags`：swapchain image 可用用途。

图像数量可以按“至少双缓冲，允许时多一张”来选择：

```cpp
uint32_t imageCount = capabilities.minImageCount + 1;

if (capabilities.maxImageCount > 0 &&
    imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
}
```

窗口尺寸选择要注意 `currentExtent`。有些平台会直接指定固定尺寸；有些平台会返回 `UINT32_MAX`，表示应用可以自己选择。

```cpp
vk::Extent2D extent;

if (capabilities.currentExtent.width !=
    std::numeric_limits<uint32_t>::max()) {
    extent = capabilities.currentExtent;
} else {
    extent.width = std::clamp<uint32_t>(
        windowWidth,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );
    extent.height = std::clamp<uint32_t>(
        windowHeight,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );
}
```

---

### 2.2.4 查询 Present Mode：`vk::PhysicalDevice::getSurfacePresentModesKHR`

目标：选择图像提交给窗口系统的策略。  
`vk::PresentModeKHR::eFifo` 类似垂直同步队列，是 Vulkan 保证可用的 present mode。`vk::PresentModeKHR::eMailbox` 通常延迟更低，适合三缓冲语义，但不保证所有平台支持。

```cpp
auto presentModes = phyDevice.getSurfacePresentModesKHR(surface);

vk::PresentModeKHR selectedPresent = vk::PresentModeKHR::eFifo;
for (const auto& mode : presentModes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
        selectedPresent = mode;
        break;
    }
}
```

`vk::PresentModeKHR` 常见枚举含义如下：

1. `vk::PresentModeKHR::eImmediate`
   - 提交后尽快显示，不等待垂直同步；
   - 如果显示设备正在扫描旧帧，可能直接切到新帧，因此容易出现 tearing（画面撕裂）；
   - 优点是等待最少、延迟低；缺点是显示稳定性较差。

2. `vk::PresentModeKHR::eMailbox`
   - 可以理解成“单槽替换队列”或低延迟三缓冲语义；
   - 如果显示系统还没来得及显示上一张待呈现图像，新提交的图像会把旧的待呈现图像覆盖掉，始终保留最新的一帧；
   - 通常不会像 `eImmediate` 那样撕裂，同时延迟往往比 `eFifo` 更低；
   - 适合交互性强的实时渲染，但并非所有平台都支持。

3. `vk::PresentModeKHR::eFifo`
   - 类似垂直同步下的先进先出队列；
   - 图像按提交顺序排队，显示系统在每次垂直刷新时取出队首图像显示；
   - 不会发生 tearing；
   - 可能因为排队而增加显示延迟；
   - **这是 Vulkan 保证可用的 present mode。**

4. `vk::PresentModeKHR::eFifoRelaxed`
   - 大部分时候行为类似 `eFifo`；
   - 但如果应用提交速度跟不上显示刷新，导致错过上一次垂直同步，那么下一次提交可以不再严格等到新的垂直同步点，可能立即显示；
   - 这样有机会降低“掉帧后继续排队”的额外延迟，但在某些时刻也可能出现 tearing；
   - 它比 `eImmediate` 更克制，比 `eFifo` 更激进。

5. `vk::PresentModeKHR::eSharedDemandRefresh`
   - 用于 shared presentable image 一类较特殊的呈现模型；
   - 图像通常不会像普通 swapchain 那样在多张 image 间轮换，而是由应用和显示系统共享；
   - 只有在特定扩展和平台场景下才会接触，普通窗口渲染通常不会使用。

6. `vk::PresentModeKHR::eSharedContinuousRefresh`
   - 同样属于 shared presentable image 相关模式；
   - 语义上比 `eSharedDemandRefresh` 更偏向持续刷新；
   - 也属于较少见的扩展场景，入门阶段通常可以忽略。

入门和常规窗口渲染最值得重点理解的，通常就是前四个：`Immediate / Mailbox / FIFO / FIFO Relaxed`。  
如果从“是否等待垂直同步”和“是否容易撕裂”来粗略理解，可以记成：

```text
Immediate      -> 最激进，低延迟，但容易撕裂
Mailbox        -> 通常低延迟，不易撕裂，像更理想的实时模式
FIFO           -> 最稳妥，不撕裂，但可能排队增加延迟
FIFO Relaxed   -> 介于 Immediate 和 FIFO 之间
```

入门阶段推荐策略是：优先 `Mailbox`，否则回退到 `FIFO`。  
这样既能在支持的平台上获得较好的交互延迟，又能保证在所有合规实现上可运行。

## 2.3 创建流程

### 2.3.1 填写 Swapchain 创建信息：`vk::SwapchainCreateInfoKHR`

目标：把前面查询到的 surface format、extent、image count、present mode 等信息写入创建参数。  
项目中 `Swapchain::queryInfo(w, h)` 负责收集这些信息，然后构造 `vk::SwapchainCreateInfoKHR`。

```cpp
vk::SwapchainCreateInfoKHR createInfo;
createInfo.setSurface(Context::GetInstance().surface)
          .setMinImageCount(info.imageCount)
          .setImageFormat(info.format.format)
          .setImageColorSpace(info.format.colorSpace)
          .setImageExtent(info.imageExtent)
          .setImageArrayLayers(1)
          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
          .setPreTransform(info.transform)
          .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
          .setPresentMode(info.present)
          .setClipped(true);
```

关键参数说明：

1. `setSurface`：指定 swapchain 绑定的窗口 surface；
2. `setMinImageCount`：请求的 swapchain image 数量；
3. `setImageFormat / setImageColorSpace`：必须来自 surface 支持的 format 列表；
4. `setImageExtent`：swapchain 图像尺寸，通常等于窗口 framebuffer 尺寸；
5. `setImageArrayLayers(1)`：普通 2D 窗口渲染使用 1 层；
6. `setImageUsage(eColorAttachment)`：表示后续 render pass 会把它当颜色附件写入；
7. `setPreTransform`：使用 surface 当前 transform，避免窗口系统额外旋转不匹配；
8. `setClipped(true)`：允许窗口系统忽略被遮挡区域的像素结果。

如果以后需要先渲染到离屏 image，再拷贝到 swapchain image，就需要额外启用 `vk::ImageUsageFlagBits::eTransferDst`。  
如果当前只做 render pass 颜色输出，`eColorAttachment` 就足够。

---

### 2.3.2 设置图像共享模式：`vk::SharingMode`

目标：处理 graphics queue family 与 present queue family 是否相同的问题。

如果 graphics 和 present 来自同一个 queue family，使用 `eExclusive`：

```cpp
createInfo.setQueueFamilyIndices(queueFamilyIndices.graphicsQueue.value())
          .setImageSharingMode(vk::SharingMode::eExclusive);
```

`eExclusive` 表示同一时刻图像只归一个 queue family 使用，性能通常更好。  
当 graphics/present 是同一个 family 时，这是最简单也最推荐的模式。

如果 graphics 和 present 来自不同 queue family，使用 `eConcurrent`：

```cpp
std::array indices = {
    queueFamilyIndices.graphicsQueue.value(),
    queueFamilyIndices.presentQueue.value()
};

createInfo.setQueueFamilyIndices(indices)
          .setImageSharingMode(vk::SharingMode::eConcurrent);
```

`eConcurrent` 允许多个 queue family 共享 swapchain image，代码更简单，但可能有额外开销。  
更高级的做法是使用 `eExclusive` 加 queue family ownership transfer，不过入门项目通常没有必要一开始就引入这部分复杂度。

---

### 2.3.3 创建 Swapchain：`vk::Device::createSwapchainKHR`

目标：在 logical device 上创建真正的 swapchain 对象。

```cpp
vk::SwapchainKHR swapchain =
    Context::GetInstance().device.createSwapchainKHR(createInfo);
```

这里的调用对象是 `vk::Device`，不是 `vk::Instance`。  
原因是 swapchain images 是设备级图像资源，后续会被 graphics queue 渲染、被 present queue 呈现。

---

### 2.3.4 获取 Swapchain Images：`vk::Device::getSwapchainImagesKHR`

目标：拿到 swapchain 内部创建的图像句柄。  
这些 `vk::Image` 由 swapchain 拥有，应用不能单独销毁它们。

```cpp
images = Context::GetInstance()
    .device
    .getSwapchainImagesKHR(swapchain);
```

注意：创建 swapchain 时请求的是 `minImageCount`，最终返回的 image 数量可能由实现决定。  
所以后续创建 image views、framebuffers、同步对象时，应以 `images.size()` 为准。

---

### 2.3.5 为每张图像创建 ImageView：`vk::Device::createImageView`

目标：把 `vk::Image` 包装成 render pass/framebuffer 可以引用的 `vk::ImageView`。  
Vulkan 中 image 是存储对象，image view 是访问视图。即使只做最简单的颜色渲染，也需要 image view。

```cpp
imageViews.resize(images.size());

for (std::size_t i = 0; i < images.size(); ++i) {
    vk::ImageSubresourceRange range;
    range.setAspectMask(vk::ImageAspectFlagBits::eColor)
         .setBaseMipLevel(0)
         .setLevelCount(1)
         .setBaseArrayLayer(0)
         .setLayerCount(1);

    vk::ImageViewCreateInfo createInfo;
    createInfo.setImage(images[i])
              .setViewType(vk::ImageViewType::e2D)
              .setFormat(info.format.format)
              .setSubresourceRange(range);

    imageViews[i] =
        Context::GetInstance().device.createImageView(createInfo);
}
```

这里的 `format` 必须与 swapchain image format 匹配。  
`aspectMask` 使用 `eColor`，因为 swapchain image 在当前项目中作为颜色附件使用，而不是 depth/stencil 附件。

---

### 2.3.6 为每张 ImageView 创建 Framebuffer：`vk::Device::createFramebuffer`

目标：把具体的 swapchain image view 绑定到 render pass 的 attachment 上。  
Render pass 描述“附件如何使用”，framebuffer 则指定“本次渲染具体用哪一张 image view”。

```cpp
framebuffers.resize(imageViews.size());

for (std::size_t i = 0; i < imageViews.size(); ++i) {
    vk::FramebufferCreateInfo createInfo;
    createInfo.setRenderPass(Context::GetInstance().renderProcess->renderPass)
              .setAttachments(imageViews[i])
              .setWidth(info.imageExtent.width)
              .setHeight(info.imageExtent.height)
              .setLayers(1);

    framebuffers[i] =
        Context::GetInstance().device.createFramebuffer(createInfo);
}
```

framebuffer 数量通常与 swapchain image 数量一致。  
每一帧通过 `acquireNextImageKHR` 得到 image index 后，就可以选择同 index 的 framebuffer 作为当前渲染目标。

## 2.4 每帧使用流程

### 2.4.1 获取可渲染图像：`vk::Device::acquireNextImageKHR`

目标：从 swapchain 中取得下一张可写入的图像索引。  
这一步通常需要一个 semaphore，用来通知 GPU：图像已经可用于渲染。

```cpp
auto result = device.acquireNextImageKHR(
    swapchain->swapchain,
    std::numeric_limits<uint64_t>::max(),
    imageAvailableSemaphore
);

uint32_t imageIndex = result.value;
```

`imageIndex` 不是帧号，而是 swapchain image 数组中的索引。  
后续选择 framebuffer、signal semaphore、present image 时都要使用这个索引。

---

### 2.4.2 渲染到对应 Framebuffer：`vk::RenderPassBeginInfo`

目标：把当前 acquired image 对应的 framebuffer 作为本帧渲染目标。

```cpp
vk::RenderPassBeginInfo renderPassBegin;
renderPassBegin.setRenderPass(renderProcess->renderPass)
               .setFramebuffer(swapchain->framebuffers[imageIndex])
               .setRenderArea({{0, 0}, swapchain->info.imageExtent})
               .setClearValues(clearValue);

cmdBuf.beginRenderPass(renderPassBegin, {});
cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->pipeline);
cmdBuf.draw(3, 1, 0, 0);
cmdBuf.endRenderPass();
```

这里真正被写入的是 swapchain image 对应的 framebuffer。  
render pass 中 attachment 的 final layout 通常会设置为 `vk::ImageLayout::ePresentSrcKHR`，表示渲染结束后图像要进入 present 阶段。

---

### 2.4.3 提交渲染命令：`vk::Queue::submit`

目标：让 graphics queue 等待“图像可用”信号，执行绘制命令，并在绘制结束后发出“渲染完成”信号。

```cpp
constexpr vk::PipelineStageFlags waitStage =
    vk::PipelineStageFlagBits::eColorAttachmentOutput;

vk::SubmitInfo submit;
submit.setWaitSemaphores(imageAvailableSemaphore)
      .setWaitDstStageMask(waitStage)
      .setCommandBuffers(cmdBuf)
      .setSignalSemaphores(renderFinishedSemaphore);

graphicsQueue.submit(submit, inFlightFence);
```

`waitDstStageMask` 表示 graphics queue 要在哪个管线阶段等待 `imageAvailableSemaphore`。  
对于 swapchain color attachment，通常使用 `eColorAttachmentOutput`。

---

### 2.4.4 提交呈现：`vk::Queue::presentKHR`

目标：让 present queue 等待“渲染完成”信号，然后把对应 image index 提交给窗口系统显示。

```cpp
vk::PresentInfoKHR present;
present.setSwapchains(swapchain->swapchain)
       .setImageIndices(imageIndex)
       .setWaitSemaphores(renderFinishedSemaphore);

vk::Result presentResult = presentQueue.presentKHR(present);
```

present 并不等价于立即显示到屏幕。  
它表示图像已经提交给窗口系统，最终何时显示由 present mode 和窗口系统调度决定。

实际工程中还需要处理 `eSuboptimalKHR` 和 `eErrorOutOfDateKHR`：

1. `eSuboptimalKHR`：swapchain 还能用，但已经不是最匹配状态，例如窗口尺寸变化；
2. `eErrorOutOfDateKHR`：swapchain 已失效，必须重建。

## 2.5 重建与销毁

### 2.5.1 何时需要重建 Swapchain

swapchain 与窗口 surface 状态强绑定。以下情况常常需要重建：

1. 窗口 resize；
2. 窗口最小化后恢复；
3. surface capabilities 改变；
4. present 返回 `vk::Result::eErrorOutOfDateKHR`；
5. 希望改变 format、present mode、image count 或 image usage。

重建不只是重新创建 `vk::SwapchainKHR`。凡是依赖 swapchain image format、extent 或 image view 的对象，都可能需要同步重建，例如：

1. swapchain image views；
2. framebuffers；
3. render pass；
4. graphics pipeline 中固定 viewport/scissor 的部分；
5. 与 swapchain image 数量绑定的同步对象。

如果使用 dynamic viewport/scissor，可以减少 pipeline 因窗口尺寸变化而重建的频率，但 framebuffers 仍然需要跟随 swapchain images 重建。

---

### 2.5.2 销毁顺序

目标：按照依赖关系反向释放资源，避免还在使用的 GPU 资源被提前销毁。

推荐顺序可以按项目当前做法理解为：先让 device 空闲，再释放 swapchain 相关资源，最后释放 surface、device、instance。

```cpp
device.waitIdle();

for (auto framebuffer : framebuffers) {
    device.destroyFramebuffer(framebuffer);
}

for (auto imageView : imageViews) {
    device.destroyImageView(imageView);
}

device.destroySwapchainKHR(swapchain);

// surface 不属于 device，但必须晚于所有依赖它的 swapchain 销毁。
instance.destroySurfaceKHR(surface);
device.destroy();
instance.destroy();
```

不要销毁 swapchain images。  
应用只销毁自己创建的 image views 和 framebuffers，swapchain images 会随着 `destroySwapchainKHR` 由驱动释放。

# 3 Surface 与 Swapchain 的关系

## 3.1 概念

Surface 和 swapchain 是窗口呈现链路中的上下游关系：

1. `Surface` 描述“显示到哪里”；
2. `Swapchain` 描述“用哪些图像轮流显示”；
3. `ImageView` 描述“如何访问这些图像”；
4. `Framebuffer` 描述“本次 render pass 具体写哪张图像”；
5. `PresentQueue` 负责“把写好的图像交给窗口系统”。

一条典型数据路径是：

```text
SDL_Window
    -> VkSurfaceKHR
    -> VkSwapchainKHR
    -> VkImage[]
    -> VkImageView[]
    -> VkFramebuffer[]
    -> RenderPass
    -> Present
```

因此，surface 更偏向“平台窗口接口”，swapchain 更偏向“渲染图像队列”。  
前者是 WSI 的入口，后者是每帧渲染和显示真正交互的对象。

## 3.2 项目初始化顺序

结合当前 Toy2D 项目，初始化链路可以概括为：

```text
SDL_CreateWindow
    -> SDL_Vulkan_GetInstanceExtensions
    -> Context::CreateInstance
    -> SDL_Vulkan_CreateSurface
    -> Context::queryQueueFamilyIndices
    -> Context::createDevice
    -> Context::InitSwapchain
    -> RenderProcess::InitRenderPass
    -> Swapchain::CreateFramebuffers
    -> RenderProcess::InitPipeline
    -> Renderer::Render
```

这个顺序背后的依赖关系是：

1. 没有 SDL window，就无法创建 surface；
2. 没有 surface，就无法判断 present queue；
3. 没有 present queue 和 swapchain device extension，就无法创建合适的 device/swapchain；
4. 没有 swapchain format，就无法确定 render pass color attachment format；
5. 没有 swapchain image views，就无法创建 framebuffers；
6. 没有 framebuffer，就无法开始窗口渲染。

## 3.3 实践建议

1. 创建 instance 前，始终从窗口库查询 required extensions，不要跨平台硬编码。
2. 选择 physical device 时，把 `VK_KHR_swapchain`、surface format、present mode、present queue 都纳入检查。
3. `FIFO` 是最安全的 present mode；`Mailbox` 可以作为可用时的优先选择。
4. swapchain image count 不要盲目固定为 2 或 3，要尊重 surface capabilities。
5. `maxImageCount == 0` 表示没有显式上限，不能直接拿它当 `std::clamp` 的上界。
6. 每个 swapchain image 都需要对应 image view；如果使用传统 render pass/framebuffer 路线，通常还需要对应 framebuffer。
7. 处理窗口 resize 时，不只重建 swapchain，也要重建依赖 swapchain format/extent/images 的对象。
8. 销毁前先 `device.waitIdle()` 或使用更精细的 fence 保证 GPU 不再访问旧 swapchain 资源。
