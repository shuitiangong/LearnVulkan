# 1 Image

## 1.1 概念

`vk::Image`（C API: `VkImage`）是 Vulkan 中用于表示多维图像数据的资源对象。  
它可以表示颜色纹理、深度缓冲、模板缓冲、离屏渲染目标、swapchain 图像、MSAA 图像、cubemap、texture array 等。

和 `vk::Buffer` 相比，`vk::Image` 不是一段简单线性内存。它有更强的图像语义：

1. 有维度：1D / 2D / 3D；
2. 有格式：例如 `eR8G8B8A8Srgb`、`eD32Sfloat`；
3. 有宽高深：`vk::Extent3D`；
4. 有 mipmap 层级：`mipLevels`；
5. 有数组层：`arrayLayers`；
6. 有采样数：`samples`，用于 MSAA；
7. 有布局：`vk::ImageLayout`；
8. 有用途：`vk::ImageUsageFlags`。

`Image` 的核心职责是保存像素或像素类似的数据。  
但是 Vulkan 不允许大多数 API 直接“随便访问 image”。访问 image 时通常还需要一个 `vk::ImageView`，用来说明这次访问把 image 看成什么格式、什么维度、哪几个 mip level、哪几个 array layer。

可以把关系理解为：

```text
vk::Image      = 真正的图像资源
vk::DeviceMemory = 图像背后的显存/内存
vk::ImageView  = 访问 image 的视图
```

一个重要区别是：**不是所有 image 都由应用自己分配内存**。

1. 普通纹理、深度图、离屏 render target：应用创建 `vk::Image`，查询内存需求，分配并绑定 `vk::DeviceMemory`；
2. swapchain images：由 `vk::SwapchainKHR` 创建和管理，应用通过 `getSwapchainImagesKHR` 取得句柄，不需要也不能手动绑定和销毁这些 images。

常见误区：

1. **把 image 当成 CPU 可直接写的数组**：多数 GPU image 使用 `eOptimal` tiling，内存布局由驱动决定，不能像普通数组一样直接写。
2. **创建 image 后忘记绑定 memory**：普通 image 创建后只是资源壳，必须 `getImageMemoryRequirements -> allocateMemory -> bindImageMemory`。
3. **usage flags 写得太少**：创建时没声明某种用途，后面不能随便拿它做对应操作。
4. **忽略 image layout**：同一张 image 在 transfer、shader sampling、color attachment、present 等阶段需要处于正确布局。
5. **手动销毁 swapchain image**：swapchain image 由 swapchain 拥有，应用只销毁对应的 image view / framebuffer。

## 1.2 创建流程

### 1.2.1 描述图像资源：`vk::ImageCreateInfo`

目标：声明要创建一张什么样的图像。  
`vk::ImageCreateInfo` 决定 image 的格式、尺寸、层级、用途、tiling 和初始布局。

```cpp
vk::ImageCreateInfo createInfo;
createInfo.setImageType(vk::ImageType::e2D)
          .setExtent({width, height, 1})
          .setMipLevels(1)
          .setArrayLayers(1)
          .setFormat(vk::Format::eR8G8B8A8Srgb)
          .setTiling(vk::ImageTiling::eOptimal)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setUsage(vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setSharingMode(vk::SharingMode::eExclusive);
```

关键参数说明：

1. `setImageType`：决定 image 是 1D、2D 还是 3D；
2. `setExtent`：设置宽、高、深度；普通 2D 图像深度为 1；
3. `setMipLevels`：mipmap 层级数量，入门阶段通常先设为 1；
4. `setArrayLayers`：数组层数，普通 2D 纹理为 1，cubemap 通常为 6；
5. `setFormat`：像素格式，必须符合后续用途要求；
6. `setTiling`：`eOptimal` 适合 GPU 高效访问，`eLinear` 更接近 CPU 线性访问；
7. `setInitialLayout`：普通新建 image 通常使用 `eUndefined`；
8. `setUsage`：声明后续用途，例如 transfer、sampled、color attachment、depth stencil attachment；
9. `setSamples`：MSAA 采样数，非 MSAA 通常为 `e1`；
10. `setSharingMode`：多 queue family 共享方式。

`usage` 必须提前规划。  
例如一张纹理如果要先通过 staging buffer 拷贝数据，再在 fragment shader 中采样，通常需要：

```cpp
vk::ImageUsageFlagBits::eTransferDst |
vk::ImageUsageFlagBits::eSampled
```

如果一张 image 要作为离屏渲染目标，通常需要：

```cpp
vk::ImageUsageFlagBits::eColorAttachment |
vk::ImageUsageFlagBits::eSampled
```

---

### 1.2.2 创建 Image 句柄：`vk::Device::createImage`

目标：在 logical device 上创建 image 对象。  
这一步只创建资源对象本身，不代表显存已经绑定完成。

```cpp
vk::Image image = device.createImage(createInfo);
```

对于普通 image，创建完成后必须继续查询内存需求并绑定内存。  
如果跳过内存绑定，后续使用这张 image 会触发验证层错误。

---

### 1.2.3 查询内存需求：`vk::Device::getImageMemoryRequirements`

目标：获取这张 image 需要多少内存、对齐要求是什么、允许使用哪些 memory type。

```cpp
vk::MemoryRequirements requirements =
    device.getImageMemoryRequirements(image);
```

常用字段：

1. `size`：需要分配的内存大小；
2. `alignment`：绑定时的对齐要求；
3. `memoryTypeBits`：可用 memory type 的位掩码。

`memoryTypeBits` 不能忽略。  
它表示这张 image 能绑定到哪些 memory type，不是所有显存类型都一定适合当前 image。

---

### 1.2.4 选择内存类型：`vk::PhysicalDeviceMemoryProperties`

目标：从物理设备提供的 memory type 中选出满足需求的类型。  
纹理、深度图、颜色附件这类 GPU 高频访问资源，通常选择 `eDeviceLocal`。

```cpp
uint32_t FindMemoryType(
    vk::PhysicalDevice physicalDevice,
    uint32_t typeBits,
    vk::MemoryPropertyFlags properties) {

    auto memoryProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        bool supported = typeBits & (1u << i);
        bool matched =
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;

        if (supported && matched) {
            return i;
        }
    }

    throw std::runtime_error("No suitable memory type found.");
}
```

常见内存属性：

1. `vk::MemoryPropertyFlagBits::eDeviceLocal`：GPU 访问快，适合最终图像资源；
2. `vk::MemoryPropertyFlagBits::eHostVisible`：CPU 可以 map；
3. `vk::MemoryPropertyFlagBits::eHostCoherent`：减少手动 flush/invalidate 的复杂度。

图片纹理的常见上传路线是：

1. CPU 把像素写入 host visible staging buffer；
2. GPU 使用 copy 命令把 staging buffer 拷贝到 device local image；
3. shader 从 device local image 采样。

这样做比直接使用 CPU 可见 image 更常见，也更符合 GPU 高效访问路径。

---

### 1.2.5 分配并绑定内存：`vk::Device::allocateMemory` / `vk::Device::bindImageMemory`

目标：为 image 分配实际内存，并把内存绑定到 image 上。

```cpp
uint32_t memoryType = FindMemoryType(
    physicalDevice,
    requirements.memoryTypeBits,
    vk::MemoryPropertyFlagBits::eDeviceLocal
);

vk::MemoryAllocateInfo allocInfo;
allocInfo.setAllocationSize(requirements.size)
         .setMemoryTypeIndex(memoryType);

vk::DeviceMemory memory = device.allocateMemory(allocInfo);
device.bindImageMemory(image, memory, 0);
```

`bindImageMemory` 的 offset 必须满足 `requirements.alignment`。  
入门阶段每个 image 单独分配一块 memory 时，offset 通常为 0。更高级的资源管理器会把多个 image/buffer 子分配到同一大块 `DeviceMemory` 中。

---

### 1.2.6 转换图像布局：`vk::ImageMemoryBarrier`

目标：让 image 在不同使用阶段处于正确 layout，并建立必要的内存可见性关系。  
Vulkan 不会自动猜测 image 接下来要做什么，layout transition 是显式同步的一部分。

例如，纹理上传前后常见布局变化是：

```text
eUndefined
    -> eTransferDstOptimal
    -> eShaderReadOnlyOptimal
```

对应的 barrier 可以写成：

```cpp
vk::ImageMemoryBarrier barrier;
barrier.setOldLayout(vk::ImageLayout::eUndefined)
       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
       .setSrcAccessMask({})
       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
       .setImage(image)
       .setSubresourceRange({
           vk::ImageAspectFlagBits::eColor,
           0, 1,
           0, 1
       });

cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eTransfer,
    {},
    nullptr,
    nullptr,
    barrier
);
```

从 transfer 写入完成后，变成 shader 可读：

```cpp
barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
       .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
       .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader,
    {},
    nullptr,
    nullptr,
    barrier
);
```

当前项目的 swapchain image layout transition 主要由 render pass attachment 描述完成：

```cpp
attachDesc.setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
```

这表示进入 render pass 前可以丢弃旧内容，render pass 结束后图像要进入 present 所需布局。  
如果后续加入纹理上传、离屏渲染、compute 写 image，就需要更频繁地手动使用 barrier 管理 layout。

## 1.3 Swapchain Image 的特殊性

swapchain images 也是 `vk::Image`，但它们不是普通应用自建 image。  
它们由 swapchain 根据 surface format、extent、image usage 等参数创建，应用只通过 device 获取句柄。

```cpp
std::vector<vk::Image> images =
    device.getSwapchainImagesKHR(swapchain);
```

对 swapchain images 的规则：

1. 不调用 `createImage` 创建它们；
2. 不调用 `allocateMemory` / `bindImageMemory` 绑定它们；
3. 不调用 `destroyImage` 销毁它们；
4. 可以为它们创建 `vk::ImageView`；
5. 可以把它们通过 framebuffer 作为 render pass 的颜色附件；
6. 随着 `destroySwapchainKHR`，这些 image 由驱动释放。

当前项目的 `Swapchain::getImages()` 正是这个流程：

```cpp
void Swapchain::getImages() {
    images = Context::GetInstance()
        .device
        .getSwapchainImagesKHR(swapchain);
}
```

这也是为什么 `Swapchain::~Swapchain()` 里只销毁 framebuffer、image view 和 swapchain，而不销毁 `images` 中的每个 `vk::Image`。

# 2 ImageView

## 2.1 概念

`vk::ImageView`（C API: `VkImageView`）是对 `vk::Image` 的访问视图。  
它不拥有图像数据，也不分配显存；它只是告诉 Vulkan：“这次访问这张 image 时，把它解释成什么格式、什么维度、哪一部分 subresource。”

为什么需要 ImageView？  
因为同一张 image 可能有多个 mip level、多个 array layer，也可能被不同用途以不同方式访问。Vulkan 把“存储资源”和“访问方式”拆开：

1. `vk::Image`：保存实际图像数据；
2. `vk::ImageView`：描述访问 image 的方式；
3. `Framebuffer / DescriptorSet`：通常绑定 image view，而不是直接绑定 image。

典型场景：

1. swapchain image 要作为 framebuffer attachment，需要 image view；
2. 纹理 image 要被 shader 采样，需要 image view；
3. depth image 要作为 depth attachment，需要 image view；
4. cubemap image 要按 cube 视图采样，需要 image view；
5. texture array 可以为某一层或某几层创建不同 view。

常见误区：

1. **认为 image view 会复制图像数据**：不会，它只是视图对象；
2. **format 随便写**：一般要与 image 创建格式兼容；
3. **aspectMask 写错**：颜色图用 `eColor`，深度图用 `eDepth`，带模板的格式还要考虑 `eStencil`；
4. **subresourceRange 范围越界**：mip level 和 array layer 范围必须在 image 创建时声明的范围内；
5. **销毁 image 后继续使用 image view**：image view 依赖 image，必须先销毁 view，再销毁 image。

## 2.2 创建流程

### 2.2.1 填写子资源范围：`vk::ImageSubresourceRange`

目标：指定这个 view 覆盖 image 的哪些 aspect、mip level 和 array layer。

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eColor)
     .setBaseMipLevel(0)
     .setLevelCount(1)
     .setBaseArrayLayer(0)
     .setLayerCount(1);
```

字段说明：

1. `aspectMask`：访问 color、depth、stencil 中的哪一部分；
2. `baseMipLevel`：从哪个 mip level 开始；
3. `levelCount`：包含多少个 mip level；
4. `baseArrayLayer`：从哪个 array layer 开始；
5. `layerCount`：包含多少个 array layer。

普通 swapchain image 没有 mipmap，也不是数组纹理，所以使用 `0, 1, 0, 1`。

---

### 2.2.2 创建颜色 ImageView：`vk::ImageViewCreateInfo`

目标：为 color image 创建一个 2D view。  
当前项目中，每个 swapchain image 都会创建一个对应的 color image view。

```cpp
vk::ImageViewCreateInfo createInfo;
createInfo.setImage(images[i])
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(info.format.format)
          .setSubresourceRange(range);

vk::ImageView view =
    Context::GetInstance().device.createImageView(createInfo);
```

关键参数：

1. `setImage`：被查看的底层 image；
2. `setViewType`：视图维度，普通窗口颜色图是 `e2D`；
3. `setFormat`：访问时使用的格式；
4. `setSubresourceRange`：访问 image 的哪一部分。

当前项目的 `Swapchain::createImageViews()` 本质上就是这一步：

```cpp
imageViews.resize(images.size());

for (size_t i = 0; i < images.size(); ++i) {
    vk::ImageSubresourceRange range;
    range.setBaseMipLevel(0)
         .setLevelCount(1)
         .setBaseArrayLayer(0)
         .setLayerCount(1)
         .setAspectMask(vk::ImageAspectFlagBits::eColor);

    vk::ImageViewCreateInfo createInfo;
    createInfo.setImage(images[i])
              .setViewType(vk::ImageViewType::e2D)
              .setFormat(info.format.format)
              .setSubresourceRange(range);

    imageViews[i] =
        Context::GetInstance().device.createImageView(createInfo);
}
```

这一步完成后，swapchain image 才能被 framebuffer 引用。

---

### 2.2.3 创建深度 ImageView：`vk::ImageAspectFlagBits::eDepth`

目标：为 depth image 创建可作为 depth attachment 使用的 view。  
当项目加入深度测试后，通常会创建一张深度 image，并为它创建 depth image view。

```cpp
vk::ImageSubresourceRange depthRange;
depthRange.setAspectMask(vk::ImageAspectFlagBits::eDepth)
          .setBaseMipLevel(0)
          .setLevelCount(1)
          .setBaseArrayLayer(0)
          .setLayerCount(1);

vk::ImageViewCreateInfo depthViewInfo;
depthViewInfo.setImage(depthImage)
             .setViewType(vk::ImageViewType::e2D)
             .setFormat(depthFormat)
             .setSubresourceRange(depthRange);

vk::ImageView depthImageView =
    device.createImageView(depthViewInfo);
```

如果 depth format 同时包含 stencil，例如 `vk::Format::eD24UnormS8Uint`，并且后续也要访问 stencil aspect，则需要把 aspect mask 扩展为：

```cpp
vk::ImageAspectFlagBits::eDepth |
vk::ImageAspectFlagBits::eStencil
```

如果只作为深度附件，通常只写 `eDepth`。

---

### 2.2.4 创建 Mipmap 或数组纹理视图

目标：理解 image view 不一定覆盖整张 image。  
如果一张纹理有多个 mip level，可以为所有 mip level 创建一个 view，也可以只为某个 mip level 创建 view。

覆盖完整 mip 链：

```cpp
vk::ImageSubresourceRange mipRange;
mipRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(mipLevels)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
```

只查看第 2 个 mip level：

```cpp
vk::ImageSubresourceRange singleMipRange;
singleMipRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
              .setBaseMipLevel(2)
              .setLevelCount(1)
              .setBaseArrayLayer(0)
              .setLayerCount(1);
```

数组纹理或 cubemap 也是类似逻辑。  
底层 image 可以包含多个 layer，而 image view 决定本次访问哪些 layer，以及它们被解释成 `e2DArray`、`eCube` 还是其他 view type。

## 2.3 ImageView 与 Framebuffer

Framebuffer 不直接绑定 `vk::Image`，而是绑定 `vk::ImageView`。  
这是因为 render pass attachment 需要知道实际访问的是 image 的哪一部分。

当前项目中，swapchain image view 被写入 framebuffer：

```cpp
vk::FramebufferCreateInfo createInfo;
createInfo.setAttachments(imageViews[i])
          .setWidth(w)
          .setHeight(h)
          .setRenderPass(Context::GetInstance().renderProcess->renderPass)
          .setLayers(1);

framebuffers[i] =
    Context::GetInstance().device.createFramebuffer(createInfo);
```

每个 framebuffer 对应一张 swapchain image view。  
渲染时通过 `acquireNextImageKHR` 得到 `imageIndex`，再选择对应 framebuffer：

```cpp
renderPassBegin.setFramebuffer(
    swapchain->framebuffers[imageIndex]
);
```

这条链路可以概括为：

```text
Swapchain
    -> vk::Image[]
    -> vk::ImageView[]
    -> vk::Framebuffer[]
    -> RenderPass Begin
```

因此 image view 是 image 进入 render pass/framebuffer 体系的必要中间层。

# 3 Image 与 ImageView 的关系

## 3.1 概念

`Image` 和 `ImageView` 是 Vulkan 图像系统中最容易混淆的一组对象。  
它们的区别可以用一句话概括：

```text
Image 负责存储，ImageView 负责解释和访问。
```

更具体地说：

1. `vk::Image` 决定数据的实际存在形式：尺寸、格式、mip levels、array layers、usage、tiling、samples；
2. `vk::ImageView` 决定某次使用如何看这张 image：view type、format、aspect、mip 范围、layer 范围；
3. `vk::DeviceMemory` 决定普通 image 背后的实际内存；
4. `vk::Sampler` 决定 shader 采样时的过滤、寻址、mipmap 策略；
5. `vk::Framebuffer` 使用 image view 作为 render pass attachment 的具体绑定对象。

一个 image 可以有多个 image view。  
例如一张包含多个 mip level 的 texture image，可以创建一个覆盖全部 mip 的 view 供 shader 正常采样，也可以创建一个只覆盖某个 mip level 的 view 用于调试或单独处理。

## 3.2 生命周期顺序

普通自建 image 的典型生命周期是：

```text
createImage
    -> getImageMemoryRequirements
    -> allocateMemory
    -> bindImageMemory
    -> createImageView
    -> 使用 image view
    -> destroyImageView
    -> destroyImage
    -> freeMemory
```

对应代码顺序：

```cpp
device.destroyImageView(imageView);
device.destroyImage(image);
device.freeMemory(imageMemory);
```

swapchain image 的生命周期不同：

```text
createSwapchainKHR
    -> getSwapchainImagesKHR
    -> createImageView
    -> createFramebuffer
    -> 使用 framebuffer
    -> destroyFramebuffer
    -> destroyImageView
    -> destroySwapchainKHR
```

对应规则：

1. swapchain image view 由应用创建，应用负责销毁；
2. swapchain framebuffer 由应用创建，应用负责销毁；
3. swapchain image 由 swapchain 拥有，应用不销毁；
4. swapchain 销毁后，原来的 swapchain images 和 image views 都不能继续使用。

## 3.3 实践建议

1. 创建 image 前先明确用途，再填写完整的 `vk::ImageUsageFlags`。
2. 普通纹理优先使用 `eOptimal` tiling 和 `eDeviceLocal` memory，配合 staging buffer 上传。
3. 所有普通 image 创建后都要绑定 memory；swapchain image 例外。
4. image layout 是资源状态的一部分，不是可忽略细节。
5. render pass 可以帮 attachment 做部分 layout transition，但纹理上传、compute 写入、transfer 操作通常需要显式 barrier。
6. 创建 image view 时重点检查 `format`、`viewType`、`aspectMask` 和 `subresourceRange`。
7. color image 使用 `eColor` aspect，depth image 使用 `eDepth` aspect，depth-stencil image 按实际访问需求选择 `eDepth` / `eStencil`。
8. framebuffer 绑定的是 image view，不是 image。
9. 销毁顺序始终反向于依赖顺序：先 view/framebuffer，再 image，最后 memory。
10. swapchain 重建时，旧 swapchain 对应的 image views 和 framebuffers 必须一起重建。
