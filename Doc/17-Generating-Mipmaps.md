# 1 Vulkan 中的 Mipmap 生成

## 1.1 概念

### 1.1.1 Mipmap 的含义

Mipmap 是同一张纹理在不同分辨率下的一组预生成图像。

对于一张二维纹理而言：

1. level 0 是原始分辨率
2. level 1 是宽高各缩小一半后的结果
3. level 2 再次缩小一半
4. 直到宽高收敛到 `1 x 1`

如果原图尺寸为 `1024 x 1024`，那么对应的 mip 链可以写成：

```text
1024 x 1024
512  x 512
256  x 256
128  x 128
...
1    x 1
```

Vulkan 并不会为这些级别分别创建多张图片，而是把它们组织在同一个 `VkImage` 的多个 mip level 中。

---

### 1.1.2 Mipmap 存在的原因

纹理映射最容易在“远处表面 + 高频纹理”这一组合下暴露采样问题。

当物体距离相机较远时，屏幕上的一个像素往往会覆盖纹理上的一大片区域。如果仍然直接从高分辨率原图中取样，就会出现典型的欠采样现象：

1. 细节跳变明显
2. 相机移动时容易闪烁
3. 重复花纹容易出现摩尔纹
4. 远处表面会呈现不稳定的高频噪声

Mipmap 的作用并不是简单地“让纹理变糊”，而是让采样器在不同观察距离下选择与当前像素覆盖范围更接近的纹理分辨率。这样做的结果是：

1. 远处表面的纹理采样更稳定
2. 高频细节在远距离下会被合理地预过滤
3. 闪烁与摩尔纹显著减轻
4. 采样缓存利用率通常也更好

除了画面稳定性之外，Mipmap 还直接影响纹理采样阶段的运行效率。

当物体距离相机较远时，屏幕上的一个像素往往只对应纹理上的较大区域。如果仍然始终从原始高分辨率纹理中取样，就会产生两类额外开销：

1. 采样器仍在访问超出当前屏幕需求的高分辨率 texel
2. 纹理缓存需要处理更多高频、离散的采样请求

这会进一步带来：

1. 更高的纹理读取带宽压力
2. 更低的 texture cache 命中率
3. 远距离表面上不必要的高分辨率采样成本

Mipmap 的性能价值正体现在这里。采样器选择更低级别的 mip 后，访问的数据规模会更贴近当前屏幕像素真正需要的分辨率，因此通常能够：

1. 降低远距离纹理采样的带宽消耗
2. 改善纹理缓存局部性
3. 避免把运行成本浪费在已经无法被屏幕分辨率有效表达的高频细节上

因此，Mipmap 并不是只服务于图像质量的附加功能，而是一项同时作用于：

1. 远距离采样稳定性
2. 纹理访问效率
3. 显存带宽利用率

的基础机制。

当然，Mipmap 也不是零成本机制。完整 mip 链会引入额外的纹理存储开销。对于二维纹理，整条链的总容量通常约为原图的 `4/3`。这一额外开销在实时渲染中通常是可以接受的，因为它换来的不仅是更稳定的远距离画面，也包括更合理的采样成本。

---

### 1.1.3 第 0 级 mipmap 与后续级别的关系

在当前工程中，第 0 级 mipmap 并不是通过 `generateMipmaps(...)` 生成的。

第 0 级的来源是 staging buffer 到 image 的一次拷贝：

```cpp
transformData2Image(*buffer, w, h);
```

这一过程只会写入：

```cpp
subsource.setMipLevel(0);
```

因此，真正的数据流转是：

```text
staging buffer
    -> copyBufferToImage
    -> mip level 0
    -> generateMipmaps(...)
    -> mip level 1, 2, 3, ...
```

这一区分非常重要。`generateMipmaps(...)` 的职责不是写入原图，而是把已经存在的 level 0 继续缩小，逐级生成后续 mip。

---

### 1.1.4 Mipmap 生成与普通纹理上传的区别

如果纹理只有一个 level，那么常见流程是：

```text
创建 image
    -> Undefined -> TransferDstOptimal
    -> copyBufferToImage
    -> TransferDstOptimal -> ShaderReadOnlyOptimal
```

而引入 mipmap 之后，流程会变成：

```text
创建支持多个 mip level 的 image
    -> 所有 level 先转到 TransferDstOptimal
    -> copyBufferToImage 只写 level 0
    -> level 0 作为 blit 源生成 level 1
    -> level 1 作为 blit 源生成 level 2
    -> ...
    -> 所有 level 最终转到 ShaderReadOnlyOptimal
```

因此，Mipmap 生成的核心变化并不在 `stbi_load(...)` 或 `copyBufferToImage(...)` 本身，而在于：

1. image 需要拥有多个 mip level
2. image usage 需要支持 blit 源与 blit 目标
3. layout transition 需要按单个 mip level 逐级推进
4. sampler 需要允许访问完整 mip 链

---

### 1.1.5 当前工程中的职责划分

当前工程中，mipmap 的职责主要分布在两个位置：

1. `Texture`
   - 负责加载原图
   - 计算 `mipLevels_`
   - 创建支持 mip 链的 `vk::Image`
   - 执行 `copyBufferToImage`
   - 调用 `generateMipmaps(...)`
   - 创建覆盖全部 mip level 的 `vk::ImageView`

2. `Material`
   - 创建 `vk::Sampler`
   - 设置 `mipmapMode`
   - 设置 `minLod / maxLod`
   - 把 image view 与 sampler 一起写入 descriptor

这意味着 mipmap 的实现并不是某一个单独函数的局部优化，而是一条从资源创建到采样状态的完整链路。

---

### 1.1.6 `vk::Filter::eLinear` 与 mipmap 不是同一件事

一个容易混淆的点是：`minFilter` / `magFilter` 的线性过滤，与 mipmap 的存在并不是一回事。

例如：

```cpp
createInfo.setMagFilter(vk::Filter::eLinear)
          .setMinFilter(vk::Filter::eLinear);
```

这表示的是同一张纹理内部的 texel 插值方式。它可以缓解近邻采样造成的块状感，但并不能解决“远距离仍然直接从高分辨率原图取样”这一问题。

Mipmap 相关的真正控制项是：

```cpp
.setMipmapMode(vk::SamplerMipmapMode::eLinear)
.setMinLod(0.0f)
.setMaxLod(texture ? static_cast<float>(texture->GetMipLevels()) : 0.0f)
```

前者决定相邻 texel 如何插值，后者决定不同 mip level 如何选择与混合。这两层过滤共同工作，才构成完整的纹理采样行为。

---

## 1.2 实现流程

### 1.2.1 纹理级数的计算

当前工程在 `Texture` 构造函数中完成 mip 级数计算：

```cpp
mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
```

这一公式的含义是：

1. 取宽高中的较大值
2. 计算它最多可以被 `2` 缩小多少次
3. 再把原始 level 0 计入总数

例如：

1. `512 x 512` 会得到 `10` 级
2. `1024 x 512` 会得到 `11` 级
3. `1 x 1` 只会得到 `1` 级

Mip 级数一旦确定，后续 image 创建、view 创建和 sampler 设置都要围绕这个值展开。

---

### 1.2.2 创建支持 mip 链的纹理 image

当前工程的 image 创建逻辑位于 `Texture::createImage(...)`：

```cpp
vk::ImageCreateInfo createInfo;
createInfo.setImageType(vk::ImageType::e2D)
          .setArrayLayers(1)
          .setMipLevels(mipLevels_)
          .setExtent({w, h, 1})
          .setFormat(image_.format)
          .setTiling(vk::ImageTiling::eOptimal)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setUsage(
              vk::ImageUsageFlagBits::eTransferSrc |
              vk::ImageUsageFlagBits::eTransferDst |
              vk::ImageUsageFlagBits::eSampled)
          .setSamples(vk::SampleCountFlagBits::e1);
```

这里有三个参数最关键。

#### `setMipLevels(mipLevels_)`

这一步决定了 image 是否真正拥有完整 mip 链。如果这里仍然写成 `1`，后续所有生成逻辑都会失去意义。

#### `setUsage(eTransferSrc | eTransferDst | eSampled)`

这里必须同时具备三类用途：

1. `eTransferDst`
   - 用于 level 0 的 `copyBufferToImage`
   - 用于 blit 生成时的目标 level

2. `eTransferSrc`
   - 用于把上一层 mip 当作 blit 源

3. `eSampled`
   - 用于最终在 fragment shader 中采样

这组 usage 的组合直接对应 mip 生成链路中的三个阶段。

---

### 1.2.3 第 0 级纹理数据的写入

原始像素数据先通过 staging buffer 上传，再写入 image 的第 0 级。当前工程中的写入代码是：

```cpp
vk::ImageSubresourceLayers subsource;
subsource.setAspectMask(vk::ImageAspectFlagBits::eColor)
         .setBaseArrayLayer(0)
         .setMipLevel(0)
         .setLayerCount(1);

vk::BufferImageCopy region;
region.setBufferImageHeight(0)
      .setBufferOffset(0)
      .setImageExtent({w, h, 1})
      .setBufferRowLength(0)
      .setImageSubresource(subsource);

cmdBuf.copyBufferToImage(buffer.buffer,
                         image_.image,
                         vk::ImageLayout::eTransferDstOptimal,
                         region);
```

这里最容易忽略的是：

```cpp
setMipLevel(0)
```

这表明 `copyBufferToImage(...)` 只负责把原图写入 level 0。其余 level 并不会自动生成，也不会从 staging buffer 中继续展开。

---

### 1.2.4 所有 mip level 的初始布局转换

在把原图写入 level 0 之前，当前工程会先把整张纹理的全部 mip level 转到 `TransferDstOptimal`：

```cpp
vk::ImageSubresourceRange range;
range.setLayerCount(1)
     .setBaseArrayLayer(0)
     .setLevelCount(mipLevels_)
     .setBaseMipLevel(0)
     .setAspectMask(vk::ImageAspectFlagBits::eColor);

barrier.setImage(image_.image)
       .setOldLayout(vk::ImageLayout::eUndefined)
       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
       .setSubresourceRange(range);
```

这一步的意义是：

1. level 0 准备接受 `copyBufferToImage(...)`
2. 后续各级别也准备接受 `blitImage(...)` 的写入

之所以在这一阶段统一处理所有 mip level，是因为此时它们都处于相同的初始状态：`Undefined`。

---

### 1.2.5 线性 blit 支持能力检查

并不是所有格式都支持线性过滤 blit，因此在生成 mipmap 之前，当前工程先检查格式能力：

```cpp
auto formatProperties = Context::Instance().phyDevice.getFormatProperties(image_.format);
if ((formatProperties.optimalTilingFeatures &
     vk::FormatFeatureFlagBits::eSampledImageFilterLinear) ==
    vk::FormatFeatureFlags{}) {
    throw std::runtime_error("Texture format does not support linear filtering");
}
```

这一检查对应 Vulkan Tutorial 中的同一要求。原因在于当前工程生成 mipmap 的方式依赖：

```cpp
vk::Filter::eLinear
```

如果底层格式不支持 linear blit，驱动并不能保证缩小过程合法。将这一条件提前验证，比在运行阶段接受未定义行为更稳妥。

---

### 1.2.6 `generateMipmaps(...)` 的循环结构

当前工程的核心实现位于：

```cpp
void Texture::generateMipmaps(uint32_t w, uint32_t h)
```

函数内部使用如下循环：

```cpp
int32_t mipWidth = static_cast<int32_t>(w);
int32_t mipHeight = static_cast<int32_t>(h);

for (uint32_t i = 1; i < mipLevels_; ++i) {
    ...
}
```

循环从 `1` 开始，意味着每次都在做：

```text
source = i - 1
destination = i
```

整个过程可以概括为：

```text
level 0 -> level 1
level 1 -> level 2
level 2 -> level 3
...
```

这也是 mipmap 生成最核心的思路：它不是从原图一次性直接推导出所有级别，而是逐级缩小。

---

### 1.2.7 单级别布局转换与 `vk::ImageMemoryBarrier`

进入循环后，当前工程先准备一个只覆盖单个 mip level 的 `subresourceRange`：

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eColor)
     .setBaseArrayLayer(0)
     .setLayerCount(1)
     .setLevelCount(1);
```

随后在每轮循环中更新：

```cpp
range.setBaseMipLevel(i - 1);
```

这意味着每次 barrier 只作用于当前作为源的那一级 mip。

这里必须坚持 `levelCount(1)`，原因是 mipmap 生成过程中的每一级布局并不相同：

1. 还未处理的级别保持在 `TransferDstOptimal`
2. 正在作为源的级别需要临时变成 `TransferSrcOptimal`
3. 已经处理完成的级别会转成 `ShaderReadOnlyOptimal`

如果一次 barrier 覆盖多个 level，就会把这些本来应当处于不同状态的子资源强行混在一起。

---

### 1.2.8 从 `TransferDst` 到 `TransferSrc`

每轮循环开始时，`i - 1` 级刚刚完成写入，因此它当前仍处于：

```text
TransferDstOptimal
```

在把它作为 blit 源之前，必须先转换成：

```text
TransferSrcOptimal
```

当前工程的代码如下：

```cpp
barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
       .setDstAccessMask(vk::AccessFlagBits::eTransferRead);

cmdBuf.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, nullptr, barrier);
```

这一转换表明：

1. 上一阶段对该 mip 的写入已经完成
2. 下一阶段将把该 mip 当作传输读取源

对于第 1 轮循环来说，它对应的正是：

```text
mip 0: TransferDst -> TransferSrc
```

---

### 1.2.9 通过 `vk::ImageBlit` 生成下一层 mip

完成源级别布局转换后，当前工程构造 `vk::ImageBlit`，并调用 `blitImage(...)`：

```cpp
vk::ImageSubresourceLayers srcSubresource;
srcSubresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
              .setMipLevel(i - 1)
              .setBaseArrayLayer(0)
              .setLayerCount(1);

vk::ImageSubresourceLayers dstSubresource;
dstSubresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
              .setMipLevel(i)
              .setBaseArrayLayer(0)
              .setLayerCount(1);

blit.setSrcSubresource(srcSubresource)
    .setDstSubresource(dstSubresource);

blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};

blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
blit.dstOffsets[1] = vk::Offset3D{
    mipWidth > 1 ? mipWidth / 2 : 1,
    mipHeight > 1 ? mipHeight / 2 : 1,
    1
};

cmdBuf.blitImage(
    image_.image, vk::ImageLayout::eTransferSrcOptimal,
    image_.image, vk::ImageLayout::eTransferDstOptimal,
    blit,
    vk::Filter::eLinear);
```

这里有三点需要特别明确。

#### 第一，源与目标是同一张 image 的不同 mip level

当前工程并没有额外创建中间纹理，而是直接在同一个 `image_.image` 内部完成逐级缩小。

#### 第二，目标分辨率按 1/2 递减

这部分逻辑由：

```cpp
mipWidth > 1 ? mipWidth / 2 : 1
mipHeight > 1 ? mipHeight / 2 : 1
```

保证。即使原图某一维已经缩小到 `1`，后续也不会继续降到 `0`。

#### 第三，过滤方式使用 `vk::Filter::eLinear`

这一步并不是纯粹的拷贝，而是带缩放的重采样，因此能够为更低级别生成平滑的预过滤结果。

---

### 1.2.10 已生成级别转入 `ShaderReadOnlyOptimal`

当第 `i` 级已经从 `i - 1` 级生成完成后，当前轮次中的 `i - 1` 级不再承担 blit 源职责，可以直接转入 shader 可读布局：

```cpp
barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
       .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
       .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
       .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

cmdBuf.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader,
    {}, {}, nullptr, barrier);
```

这一步之后，该级别就已经进入了最终采样状态。

因此，在一个 `8 x 8 -> 4 x 4 -> 2 x 2 -> 1 x 1` 的例子里，状态推进可以写成：

```text
生成 mip 1 后：mip 0 -> ShaderReadOnly
生成 mip 2 后：mip 1 -> ShaderReadOnly
生成 mip 3 后：mip 2 -> ShaderReadOnly
循环结束后：mip 3 -> ShaderReadOnly
```

---

### 1.2.11 最后一级的单独处理

循环内部每一轮只会把 `i - 1` 级切到 `ShaderReadOnlyOptimal`。这意味着最后一级虽然已经被写入，但不会在循环中被再次作为源使用，因此它需要单独处理。

当前工程的代码如下：

```cpp
range.setBaseMipLevel(mipLevels_ - 1);
barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

cmdBuf.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader,
    {}, {}, nullptr, barrier);
```

这一步保证整条 mip 链在函数结束后都处于统一的采样状态：

```text
ShaderReadOnlyOptimal
```

如果遗漏这一步，最后一级的 layout 会停留在 `TransferDstOptimal`，从而在采样链路上留下不完整状态。

---

### 1.2.12 覆盖完整 mip 链的 image view

当前工程中，image view 的创建也已经与 mipmap 对齐：

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eColor)
     .setBaseArrayLayer(0)
     .setLayerCount(1)
     .setLevelCount(mipLevels_)
     .setBaseMipLevel(0);
```

这里的 `levelCount(mipLevels_)` 非常关键。因为 descriptor 中绑定的是 image view，而不是裸 `VkImage`。如果 view 只覆盖 level 0，那么即使底层 image 中存在完整 mip 链，采样器也无法访问这些级别。

---

### 1.2.13 Sampler 对 mipmap 的接入

Mipmap 的数据准备完成之后，是否能够被真正使用，还取决于 sampler 的配置。当前工程中的 sampler 创建位于 `Material::createSampler()`：

```cpp
vk::SamplerCreateInfo createInfo;
createInfo.setMagFilter(vk::Filter::eLinear)
          .setMinFilter(vk::Filter::eLinear)
          .setAddressModeU(vk::SamplerAddressMode::eRepeat)
          .setAddressModeV(vk::SamplerAddressMode::eRepeat)
          .setAddressModeW(vk::SamplerAddressMode::eRepeat)
          .setAnisotropyEnable(false)
          .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
          .setUnnormalizedCoordinates(false)
          .setCompareEnable(false)
          .setMipLodBias(0.0f)
          .setMinLod(0.0f)
          .setMaxLod(texture ? static_cast<float>(texture->GetMipLevels()) : 0.0f)
          .setMipmapMode(vk::SamplerMipmapMode::eLinear);
```

这里真正与 mip 链选择有关的是：

1. `setMipmapMode(vk::SamplerMipmapMode::eLinear)`
   - 表示可以在相邻 mip 级别之间做线性混合

2. `setMinLod(0.0f)`
   - 允许从 level 0 开始采样

3. `setMaxLod(texture ? static_cast<float>(texture->GetMipLevels()) : 0.0f)`
   - 允许访问整条 mip 链

当前工程在创建纹理后会重新创建 sampler：

```cpp
texture = std::make_unique<Texture>(path);
createSampler();
```

这一步的意义在于让 `maxLod` 与当前纹理的 mip 级数保持一致，而不是停留在旧状态。

---

### 1.2.14 运行效果与验证思路

Mipmap 是否真正生效，最直观的观察对象并不是近处表面，而是远处的大面积纹理区域。当前工程默认加载：

```cpp
auto texturePath = toy2d::ResolveAssetPath("assets/textures/viking_room.png");
```

并将其应用到：

```cpp
auto roomMesh = toy2d::LoadObjMesh(modelPath);
```

对应的 `viking_room` 模型上。

拉远相机后，若 mipmap 工作正常，通常会看到以下现象：

1. 远处表面的细节闪烁减轻
2. 条纹、花纹和边缘附近的摩尔纹减少
3. 表面看起来会更稳定，而不是随着相机移动频繁跳变

当前工程中的相机初始位置为：

```cpp
camera.SetPosition(0.0f, 0.0f, 2.8f);
```

因此在运行时继续后退观察模型，是最直接的效果验证方式。

---

## 1.3 总结

当前工程中的 mipmap 生成功能，可以概括为一条完整的数据与状态转换链：

```text
加载原始图像
    -> 计算 mipLevels_
    -> 创建支持多个 mip level 的 vk::Image
    -> 所有 level: Undefined -> TransferDstOptimal
    -> staging buffer 写入 level 0
    -> level 0 -> level 1
    -> level 1 -> level 2
    -> ...
    -> 所有 level 最终转到 ShaderReadOnlyOptimal
    -> 创建覆盖完整 mip 链的 image view
    -> sampler 允许访问并混合这些 level
    -> fragment shader 在不同距离下选择合适的纹理分辨率
```

这一节完成的并不是单一的“缩图”函数，而是把纹理资源从“只有原图可采样”扩展到“具备完整 mip 链并可被采样器自动选择”的完整采样路径。

在此基础上继续扩展：

1. anisotropic filtering
2. 纹理数组与 cube map 的 mip 链
3. 更复杂的离线资源预处理
4. 多队列传输与图像布局管理抽象

都会顺畅得多。

---

参考教程：<https://vulkan-tutorial.com/Generating_Mipmaps>
