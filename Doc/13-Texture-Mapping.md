# 1 Texture Mapping

## 1.1 概念

### 1.1.1 Texture mapping 到底在做什么

Texture mapping 的本质不是“给模型贴一张图”这么简单，而是把两类原本独立的数据连接起来：

1. 几何体表面的参数坐标，也就是 `UV`
2. GPU 可采样的纹理资源，也就是 `image + image view + sampler`

当前项目里的网格顶点结构如下：

```cpp
struct Vertex final {
    Vec position;
    Vec texcoord;
};
```

四边形网格同时给出了顶点位置和 UV：

```cpp
inline Mesh CreateQuadMesh() {
    return Mesh{
        {
            Vertex{{-0.5f, -0.5f}, {0, 0}},
            Vertex{{0.5f, -0.5f}, {1, 0}},
            Vertex{{0.5f, 0.5f}, {1, 1}},
            Vertex{{-0.5f, 0.5f}, {0, 1}},
        },
        {0, 1, 3, 1, 2, 3}
    };
}
```

这组数据表示：

1. 左下角顶点采样 `(0, 0)`
2. 右下角顶点采样 `(1, 0)`
3. 右上角顶点采样 `(1, 1)`
4. 左上角顶点采样 `(0, 1)`

光栅化阶段会在三角形内部对这些 UV 做插值，fragment shader 再根据插值结果执行 `texture(...)` 采样。  
所以 Texture mapping 的核心链路其实是：

```text
顶点提供 UV
    -> vertex shader 输出 UV
    -> rasterizer 插值
    -> fragment shader 得到每个片元的采样坐标
    -> sampler2D 从纹理中取色
```

---

### 1.1.2 为什么纹理资源通常用 `vk::Image`，而不是直接让 shader 读 `vk::Buffer`

从 API 能力上说，Buffer 也能承载原始像素字节。  
但“承载字节”不等于“适合做纹理采样”。

Texture mapping 更适合使用 `vk::Image`，原因主要有三点：

1. Image 天然是按 1D / 2D / 3D texel 空间定义的资源
2. Image 可以配合专门的采样器对象做过滤、寻址和 mip 选择
3. GPU 对图像采样路径有专门优化，访问模式与普通 buffer 读取不同

当前项目中的 `Texture` 封装就是标准的图像资源组合：

```cpp
class Texture final {
public:
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
};
```

这意味着项目已经把“纹理像素的正式驻留形态”确定为 `vk::Image`，而不是长期保留在 staging buffer 中。

---

### 1.1.3 Texture mapping 在 Vulkan 中实际拆成哪些对象

Vulkan 没有一个单独叫“Texture”的原生对象。  
一张最终可采样的纹理，通常由以下几个对象共同组成：

1. `vk::Image`
   - 保存 texel 数据
2. `vk::DeviceMemory`
   - 为 image 提供实际内存
3. `vk::ImageView`
   - 告诉 shader 如何看待这张 image
4. `vk::Sampler`
   - 定义过滤、寻址、归一化坐标、mipmap 采样规则
5. `vk::DescriptorSet`
   - 把 image view 和 sampler 绑定到 shader 入口

当前项目把它们拆在两个类里：

```cpp
class Texture final {
public:
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
};
```

```cpp
class Material {
public:
    std::unique_ptr<Texture> texture;
    vk::Sampler sampler = nullptr;
};
```

这种拆分把像素存储、采样规则和 descriptor 绑定关系明确分开，资源职责会更清晰。

---

### 1.1.4 `UV` 的含义，以及为什么它通常是归一化坐标

`UV` 表示纹理空间坐标，不是屏幕坐标，也不是世界坐标。  
在最常见的二维纹理场景里：

```text
U -> 纹理水平方向
V -> 纹理垂直方向
```

当前项目使用的是归一化采样坐标，因为 sampler 中设置了：

```cpp
createInfo.setUnnormalizedCoordinates(false)
```

这意味着 shader 中传入的坐标通常按 `[0, 1]` 范围解释：

1. `(0, 0)` 对应纹理一角
2. `(1, 1)` 对应对角
3. `(0.5, 0.5)` 对应纹理中心附近

当前项目中，四个顶点的位置坐标和四个角的 UV 是一一对应的：

1. `(-0.5, -0.5)` 对应 `(0, 0)`
2. `(0.5, -0.5)` 对应 `(1, 0)`
3. `(0.5, 0.5)` 对应 `(1, 1)`
4. `(-0.5, 0.5)` 对应 `(0, 1)`

使用归一化坐标的好处很明显：

1. 不依赖纹理的实际分辨率
2. 同一组 UV 可以复用到不同尺寸纹理
3. 材质和网格解耦更自然

这也是实际工程里最常见的做法。

---

### 1.1.5 `tiling` 和 `layout` 不是一回事

Texture mapping 里最容易混淆的两个词就是：

1. `vk::ImageTiling`
2. `vk::ImageLayout`

两者关注的问题完全不同。

#### 1.1.5.1 `tiling` 解决的是“图像在内存里如何排布”

当前项目创建纹理 image 时使用：

```cpp
.setTiling(vk::ImageTiling::eOptimal)
```

`tiling` 的含义是 texel 在内存中的物理排布方式。  
常见的两种是：

1. `eLinear`
   - 更接近逐行线性排布，便于 CPU 直接理解
2. `eOptimal`
   - 由实现决定具体排布，目标是 GPU 访问效率更高

当前项目采用 staging buffer 上传，再让 shader 读取正式图像，因此选择 `eOptimal` 是正确方向。

#### 1.1.5.2 `layout` 解决的是“当前图像正处于哪种使用状态”

当前项目会经历三种 layout：

1. `vk::ImageLayout::eUndefined`
2. `vk::ImageLayout::eTransferDstOptimal`
3. `vk::ImageLayout::eShaderReadOnlyOptimal`

它们不是内存排布格式，而是图像在 GPU 管线中的当前用途声明：

```text
eUndefined
    -> 初始状态，不保证保留旧内容

eTransferDstOptimal
    -> 适合作为 copyBufferToImage 的目标

eShaderReadOnlyOptimal
    -> 适合作为 shader sampled image 被读取
```

因此可以把两者区分成：

```text
tiling
    -> 图像在内存里怎么排

layout
    -> 图像当前准备拿来做什么
```

这个区分非常关键。  
很多初学者会把“为什么要切 layout”和“为什么要用 optimal tiling”混成一个问题，实际上它们是两套不同层面的约束。

---

### 1.1.6 为什么新建 image 之后不能立刻采样

当前项目创建 image 时指定的是：

```cpp
.setInitialLayout(vk::ImageLayout::eUndefined)
```

这意味着新建图像并不自动处于“可采样”状态。  
如果没有后续的 layout transition，就不能直接把它拿去做：

```glsl
texture(texSampler, Texcoord)
```

原因是 Vulkan 要求应用显式声明资源状态。  
图像作为拷贝目标、颜色附件、深度附件、shader sampled resource 时，最优 layout 并不相同。  
所以 Texture mapping 的上传流程里，layout transition 是不可跳过的一步。

---

### 1.1.7 `vk::ImageView` 的作用

shader 不会直接通过 `vk::Image` 句柄采样。  
真正暴露给 descriptor 和 shader 的，是 `vk::ImageView`。

当前项目的 image view 创建如下：

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eColor)
     .setBaseArrayLayer(0)
     .setLayerCount(1)
     .setLevelCount(1)
     .setBaseMipLevel(0);

createInfo.setImage(image)
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(vk::Format::eR8G8B8A8Srgb)
          .setSubresourceRange(range);

view = Context::Instance().device.createImageView(createInfo);
```

这一步定义了三件事：

1. 当前 image 被当作 `2D` 纹理看待
2. 当前视图访问的是颜色子资源，而不是深度子资源
3. 当前只覆盖 `baseMipLevel = 0`、`levelCount = 1`、`layerCount = 1`

如果后续引入 mipmaps、array texture、cube map，ImageView 的配置就会直接决定 shader 看到的是哪个子资源区间。

---

### 1.1.8 `vk::Sampler` 的作用

在 Vulkan 里：

1. `Image / ImageView` 解决“从哪张图读”
2. `Sampler` 解决“怎么读”

当前项目的采样器配置如下：

```cpp
createInfo.setMagFilter(vk::Filter::eLinear)
          .setMinFilter(vk::Filter::eLinear)
          .setAddressModeU(vk::SamplerAddressMode::eRepeat)
          .setAddressModeV(vk::SamplerAddressMode::eRepeat)
          .setAddressModeW(vk::SamplerAddressMode::eRepeat)
          .setAnisotropyEnable(false)
          .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
          .setUnnormalizedCoordinates(false)
          .setCompareEnable(false)
          .setMipmapMode(vk::SamplerMipmapMode::eLinear);
```

这几项配置需要分开理解。

#### 1.1.8.1 `magFilter` / `minFilter`

它们决定放大和缩小时如何从周围 texel 估计最终颜色。  
当前项目使用 `eLinear`，表示做线性过滤，而不是直接取最近 texel。

#### 1.1.8.2 `addressModeU/V/W`

它们决定坐标越界时如何处理。  
当前项目使用 `eRepeat`，所以当 UV 超过 `[0, 1]` 时，纹理会重复平铺。

如果把纹理坐标放大到超过 `1.0`，再使用 repeat，就会明显看到纹理平铺效果。

#### 1.1.8.3 `unnormalizedCoordinates`

当前项目设为 `false`，也就是使用归一化坐标。  
因此 `UV` 按 `[0, 1]` 解释，而不是按 `[0, texWidth)` 和 `[0, texHeight)` 解释。

#### 1.1.8.4 `anisotropyEnable`

当前项目关闭了各向异性过滤：

```cpp
.setAnisotropyEnable(false)
```

当前项目还没有启用各向异性过滤，因此这里只保留了最基础的线性过滤方案。  
如果后续需要提高斜向采样质量，再补充 `samplerAnisotropy` 特性检查与 `maxAnisotropy` 配置即可。

#### 1.1.8.5 `mipmapMode`

当前项目设置了：

```cpp
.setMipmapMode(vk::SamplerMipmapMode::eLinear)
```

但 image 本身只有：

```cpp
.setMipLevels(1)
```

所以当前实现还没有真正进入多级 mip 采样场景。  
这也是后续继续扩展 Texture mapping 时最自然的下一步之一。

---

### 1.1.9 `Combined image sampler` 和 GLSL `sampler2D` 的对应关系

Vulkan 中的纹理绑定常见做法是 `combined image sampler descriptor`。  
它的含义不是引入一种新的 image 对象，而是把：

1. `image view`
2. `sampler`

作为一个 descriptor 单元暴露给 shader。

当前项目的 descriptor set layout 第二个 set 是：

```cpp
vk::DescriptorSetLayoutBinding set1Binding;
set1Binding.setBinding(0)
    .setDescriptorCount(1)
    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
    .setStageFlags(vk::ShaderStageFlagBits::eFragment);
```

fragment shader 与它对应的是：

```glsl
layout(set = 1, binding = 0) uniform sampler2D texSampler;
```

两边的关系可以精确写成：

```text
DescriptorType::eCombinedImageSampler
    <-> GLSL sampler2D
```

当前项目把这两类资源拆成两个 set：

1. `set = 0`
   - MVP dynamic uniform buffer
2. `set = 1`
   - 纹理 combined image sampler

这种拆法对“按对象变化的变换数据”和“按材质变化的纹理资源”做了更清晰的分层，后续扩展材质系统时也更容易继续追加 binding。

---

### 1.1.10 Texture mapping 中几个常见误区

#### 1.1.10.1 `vk::Image` 创建成功，不代表 shader 已经能采样

还必须具备：

1. 正确的 layout
2. image view
3. sampler
4. descriptor 绑定
5. 绘制时正确 bind 到 pipeline layout

#### 1.1.10.2 `UV` 越界并不一定出错

如果 address mode 是 `repeat`，越界会平铺；  
如果是 `clamp to edge`，越界会取边缘颜色；  
如果是 `clamp to border`，越界会返回边框颜色。

所以“`UV > 1` 就错了”并不是 Vulkan 纹理系统的正确理解。

#### 1.1.10.4 纹理方向经常是调试难点

图像文件坐标系、建模软件导出的 UV 约定、屏幕空间 Y 方向，以及 `stb_image` 的读取方向不一定一致。  
Texture mapping 出现“上下颠倒”时，问题通常不在 sampler，而在：

1. 贴图坐标约定
2. 图片加载时的翻转策略
3. shader 中对 UV 的处理

当前项目没有对加载结果做额外翻转逻辑，因此默认遵循当前资源与网格的既有约定。

## 1.2 流程

### 1.2.1 在顶点中加入 `texcoord`，并通过 vertex input 把它接入 shader

Texture mapping 的第一步不是创建纹理对象，而是先让网格带上 UV。  
当前项目的 `Vertex` 中已经包含 `texcoord`：

```cpp
struct Vertex final {
    Vec position;
    Vec texcoord;
};
```

随后在 `shader_program::initVertexInputDescriptions()` 中，项目把它映射到 `location = 1`：

```cpp
vertexAttributes_[0].setBinding(0)
                    .setFormat(vk::Format::eR32G32Sfloat)
                    .setLocation(0)
                    .setOffset(0);

vertexAttributes_[1].setBinding(0)
                    .setFormat(vk::Format::eR32G32Sfloat)
                    .setLocation(1)
                    .setOffset(offsetof(Vertex, texcoord));
```

binding 描述声明整条顶点步长是 `sizeof(Vertex)`：

```cpp
vertexBindings_[0].setBinding(0)
                  .setStride(sizeof(Vertex))
                  .setInputRate(vk::VertexInputRate::eVertex);
```

到这一步，GPU 已经知道：

1. `location = 0` 读取 `position`
2. `location = 1` 读取 `texcoord`

这正是后续纹理坐标能进入 shader 的前提。

---

### 1.2.2 在 vertex shader 中透传 UV，并理解“插值”才是片元采样坐标的来源

当前项目的 vertex shader 如下：

```glsl
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexcoord;
layout(location = 0) out vec2 outTexcoord;

layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 project;
    mat4 view;
    mat4 model;
} ubo;

void main() {
    gl_Position = ubo.project * ubo.view * ubo.model * vec4(inPosition, 0.0, 1.0);
    outTexcoord = inTexcoord;
}
```

这里最重要的一行是：

```glsl
outTexcoord = inTexcoord;
```

它的意义不是简单地“把值传下去”，而是把顶点级属性交给 rasterizer。  
进入三角形内部以后，`outTexcoord` 会被自动插值，fragment shader 看到的是每个片元对应位置的连续 UV。

如果怀疑 UV 有问题，可以临时把 fragment shader 改成直接输出纹理坐标颜色。  
例如输出 `vec4(Texcoord, 0.0, 1.0)`，就能立刻看出插值是否正确。  
这种把中间数据可视化为颜色的办法，在 shader 调试里非常实用。

---

### 1.2.3 使用 `stb_image` 读取图片，并先写入 staging buffer

当前项目在 `Texture` 构造函数中读取图片：

```cpp
int w;
int h;
int channel;
stbi_uc* data = stbi_load(filename.data(), &w, &h, &channel, STBI_rgb_alpha);
if (!data) {
    throw std::runtime_error(std::string("failed to load texture image: ") + stbi_failure_reason());
}
```

`STBI_rgb_alpha` 的意义是强制把结果整理成 RGBA8，这样可以统一后续 Vulkan 图像格式与像素大小计算。  
于是像素总字节数就是：

```cpp
size_t size = w * h * 4;
```

随后项目创建一块 host visible 的 staging buffer：

```cpp
std::unique_ptr<Buffer> buffer(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
                               size,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                               vk::MemoryPropertyFlagBits::eHostCoherent
                            ));
buffer->WriteData(data, size);
```

这一步对应的职责非常明确：

```text
CPU 内存中的像素数组
    -> staging buffer
    -> 后续 copy 到正式纹理 image
```

这里依然只是“上传准备”，还不是“可采样纹理”。

---

### 1.2.4 创建 `vk::Image`，明确纹理的维度、格式、用途和初始状态

当前项目在 `Texture::createImage()` 中创建二维纹理：

```cpp
createInfo.setImageType(vk::ImageType::e2D)
          .setArrayLayers(1)
          .setMipLevels(1)
          .setExtent({w, h, 1})
          .setFormat(vk::Format::eR8G8B8A8Srgb)
          .setTiling(vk::ImageTiling::eOptimal)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
          .setSamples(vk::SampleCountFlagBits::e1);
```

这里几项配置都必须对应起来理解。

#### 1.2.4.1 `imageType` 和 `extent`

`e2D` 与 `{w, h, 1}` 表示当前资源是一张二维纹理。  
`depth = 1` 不是可省略项，而是二维纹理的正确三维 extent 形式。

#### 1.2.4.2 `mipLevels` 和 `arrayLayers`

当前项目都设为 `1`，说明：

1. 没有 mip 链
2. 不是数组纹理

#### 1.2.4.3 `format`

当前项目选的是：

```cpp
vk::Format::eR8G8B8A8Srgb
```

这和 `stbi_load(..., STBI_rgb_alpha)` 一致，保证上传数据与图像格式匹配。  
同时这也意味着当前纹理在颜色空间上按 sRGB 采样。

#### 1.2.4.4 `usage`

当前项目需要两种用途：

1. 作为 `copyBufferToImage` 的目标
2. 作为 shader sampled image

所以必须包含：

```cpp
vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
```

少任何一个都不对：

1. 少 `eTransferDst`，上传路径不合法
2. 少 `eSampled`，shader 采样路径不合法

#### 1.2.4.5 `initialLayout`

当前项目使用 `eUndefined`，因为这张图像刚创建时并不需要保留任何历史像素。  
后续真正的像素内容会通过 buffer copy 写进去。

这里之所以不把 `initialLayout` 直接设成最终要使用的：

```cpp
vk::ImageLayout::eShaderReadOnlyOptimal
```

主要有三层原因。

第一，这张 image 刚创建出来时还没有任何有效纹理数据。  
当前项目真正的像素内容来自：

```text
stb_image -> staging buffer -> copyBufferToImage -> vk::Image
```

在 `copyBufferToImage` 执行之前，image 里的内容并不是“已经可给 shader 读取的纹理”，只是刚分配完成的一块图像资源。既然旧内容本来就没有意义，就没有必要为它声明一个“已经准备好给 shader 读”的 layout。

第二，接下来马上要做的第一件事不是 shader read，而是 transfer write。  
当前上传路径要求这张 image 先进入：

```cpp
vk::ImageLayout::eTransferDstOptimal
```

然后才能合法执行：

```cpp
copyBufferToImage(...)
```

也就是说，图像创建后的第一个真实用途是“作为拷贝目标接收像素”，不是“作为采样纹理被片元着色器读取”。

第三，把 `initialLayout` 直接设成最终 layout，并不会跳过中间过程。  
它不会自动完成：

1. 像素上传
2. layout transition
3. 访问同步

如果一开始就写成 `eShaderReadOnlyOptimal`，后面仍然要再切回 `eTransferDstOptimal` 才能上传数据，流程反而会变成：

```text
ShaderReadOnlyOptimal
    -> TransferDstOptimal
    -> copyBufferToImage
    -> ShaderReadOnlyOptimal
```

这比当前的：

```text
Undefined
    -> TransferDstOptimal
    -> copyBufferToImage
    -> ShaderReadOnlyOptimal
```

多了一层没有实际收益的资源状态声明。

因此，`eUndefined` 的含义不是“随便乱写一个初始值”，而是非常明确地告诉 Vulkan：  
当前不关心旧内容，图像接下来会先被写入，再在写入完成后切换到真正用于采样的 layout。

---

### 1.2.5 为 `vk::Image` 分配并绑定 `vk::DeviceMemory`

图像对象和内存分配依然是分离的。  
当前项目的分配逻辑如下：

```cpp
auto requirements = device.getImageMemoryRequirements(image);
allocInfo.setAllocationSize(requirements.size);

auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
allocInfo.setMemoryTypeIndex(index);

memory = device.allocateMemory(allocInfo);
```

随后绑定：

```cpp
Context::Instance().device.bindImageMemory(image, memory, 0);
```

这里选择 `eDeviceLocal` 非常重要。  
它表明正式纹理资源是为了 GPU 高效读取而存在，而不是为了 CPU 随时 map。

因此更合适的上传路径是：

```text
HostVisible staging buffer
    -> DeviceLocal texture image
```

这样既保留了 CPU 写入便利性，也能让正式纹理资源更适合 GPU 读取。

---

### 1.2.6 先把 image 从 `eUndefined` 转成 `eTransferDstOptimal`

在看这段代码之前，需要先把：

```cpp
cmdBuf.pipelineBarrier(...)
```

这条命令说清楚。  
`pipelineBarrier` 的作用，是在命令缓冲里插入一条显式同步命令，用来声明：

1. 哪些阶段之前的访问必须先完成
2. 哪些阶段之后的访问要等待前面的结果
3. 某个 buffer 或 image 需要在这条同步点上切换资源状态

在当前项目里，纹理 image 会依次经历：

```text
eUndefined
    -> eTransferDstOptimal
    -> copyBufferToImage
    -> eShaderReadOnlyOptimal
```

这些状态变化不是自动发生的，而是要靠 `cmdBuf.pipelineBarrier(...)` 显式写进命令缓冲。

`pipelineBarrier` 本身负责建立同步点，真正描述“要切换哪张 image、从什么 layout 切到什么 layout”的，是它接收的 `vk::ImageMemoryBarrier` 参数。  
这里使用的 `vk::ImageMemoryBarrier`，会同时描述三件事：

1. 这次同步操作针对哪张 image、哪个 subresource range
2. 资源要从什么旧状态切到什么新状态
3. 哪类访问必须先完成，后面又允许哪类访问开始

当前项目里，layout transition 不是直接“改一个 layout 字段”，而是通过：

```cpp
vk::ImageMemoryBarrier barrier;
cmdBuf.pipelineBarrier(..., barrier);
```

把“同步点”和“image 状态变化”一起提交给命令缓冲。

当前项目在 `transitionImageLayoutFromUndefine2Dst()` 中做第一次 layout transition：

```cpp
barrier.setImage(image)
       .setOldLayout(vk::ImageLayout::eUndefined)
       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
       .setSubresourceRange(range);

cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                       vk::PipelineStageFlagBits::eTransfer,
                       {}, {}, nullptr, barrier);
```

这次调用里，每个参数都有明确含义：

1. 第 1 个参数 `vk::PipelineStageFlagBits::eTopOfPipe`
   - `srcStageMask`
   - 表示 barrier 之前要等待的源阶段
2. 第 2 个参数 `vk::PipelineStageFlagBits::eTransfer`
   - `dstStageMask`
   - 表示 barrier 之后将要发生的目标阶段
3. 第 3 个参数 `{}`
   - `dependencyFlags`
   - 当前没有使用特殊依赖标志，因此保持空
4. 第 4 个参数 `{}`
   - memory barriers
   - 当前没有额外的全局内存 barrier
5. 第 5 个参数 `nullptr`
   - buffer memory barriers
   - 当前没有 buffer 需要在这里切换访问状态
6. 第 6 个参数 `barrier`
   - image memory barriers
   - 当前真正关心的是 image 的 layout transition，因此把前面配置好的 `vk::ImageMemoryBarrier` 传进去

也就是说，这条命令的实际语义是：

```text
从 TopOfPipe 到 Transfer 建立一个同步点
    -> 不额外处理全局 memory barrier
    -> 不处理 buffer barrier
    -> 只处理这一个 image barrier
```

这一步里，最关键的是 stage mask 和 access mask 的配对关系。

`vk::ImageMemoryBarrier` 这几个设置项也需要逐个对应来看：

1. `setImage(image)`
   - 指定要切换状态的是哪一张 image
2. `setOldLayout(vk::ImageLayout::eUndefined)`
   - 声明这张 image 目前处于什么旧 layout
3. `setNewLayout(vk::ImageLayout::eTransferDstOptimal)`
   - 声明这张 image 接下来要进入什么新 layout
4. `setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)`
   - 当前不做 queue family ownership transfer，所以忽略
5. `setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)`
   - 同样表示这里不处理队列家族所有权转移
6. `setDstAccessMask(vk::AccessFlagBits::eTransferWrite)`
   - 声明 barrier 之后，这张 image 将会被 transfer 阶段写入
7. `setSubresourceRange(range)`
   - 声明这次状态切换覆盖 image 的哪一部分子资源

这里没有设置 `srcAccessMask`，是因为旧 layout 是 `eUndefined`。  
当前并不要求等待某个已经发生过的有效读写结果，也不要求保留旧内容，所以不需要再声明一组“之前必须完成的访问类型”。

其中最容易一带而过的是 `setSubresourceRange(range)`。  
当前项目里的 `range` 定义如下：

```cpp
vk::ImageSubresourceRange range;
range.setLayerCount(1)
     .setBaseArrayLayer(0)
     .setLevelCount(1)
     .setBaseMipLevel(0)
     .setAspectMask(vk::ImageAspectFlagBits::eColor);
```

这段配置表示：

1. 只处理颜色子资源
2. 从 array layer 0 开始
3. 只覆盖 1 个 layer
4. 从 mip level 0 开始
5. 只覆盖 1 个 mip level

由于当前项目的纹理就是一张普通的 2D 颜色纹理，没有 mip 链，也没有数组层，所以这里只需要覆盖：

```text
color aspect
    + mip 0
    + layer 0
```

#### 1.2.6.1 为什么 `srcStage = eTopOfPipe`

旧状态是 `eUndefined`，当前并不依赖前面某个真实写入结果。  
所以把同步起点放在 `TopOfPipe` 是合理的简化写法。

#### 1.2.6.2 为什么 `dstStage = eTransfer`

因为接下来马上要执行的是：

```cpp
copyBufferToImage(...)
```

也就是 transfer 阶段的写操作。

#### 1.2.6.3 为什么 `dstAccessMask = eTransferWrite`

因为新 layout 对应的是“作为 transfer 目标被写入”。  
这里声明的就是接下来需要获得的访问权限。

这一组 barrier 的语义可以概括为：

```text
从不关心旧内容的 undefined 状态
    -> 进入允许 transfer write 的目标状态
```

---

### 1.2.7 用 `copyBufferToImage` 把 staging buffer 中的像素复制进图像

当前项目通过一次即时命令执行完成 buffer 到 image 的拷贝：

```cpp
vk::BufferImageCopy region;
vk::ImageSubresourceLayers subsource;
subsource.setAspectMask(vk::ImageAspectFlagBits::eColor)
         .setBaseArrayLayer(0)
         .setMipLevel(0)
         .setLayerCount(1);
region.setBufferImageHeight(0)
      .setBufferOffset(0)
      .setImageExtent({w, h, 1})
      .setBufferRowLength(0)
      .setImageSubresource(subsource);
cmdBuf.copyBufferToImage(buffer.buffer, 
                         image, 
                         vk::ImageLayout::eTransferDstOptimal,
                         region);
```

这里有两个经常被忽略的细节。

#### 1.2.7.1 `bufferRowLength = 0` 和 `bufferImageHeight = 0`

这表示 buffer 里的图像数据按紧密排列处理，不额外指定行跨度。  
对 `stbi_load` 读取出的连续 RGBA 数据来说，这是最自然的设置。

#### 1.2.7.2 `ImageSubresourceLayers`

当前只复制颜色面、mip 0、layer 0。  
因为当前项目既没有深度纹理，也没有数组层，也没有 mip 链。

---

### 1.2.8 再把 image 从 `eTransferDstOptimal` 转成 `eShaderReadOnlyOptimal`

上传完成后，图像依然处于 transfer 目标状态。  
此时如果马上交给 fragment shader 采样，状态依然不对。

当前项目在 `transitionImageLayoutFromDst2Optimal()` 中完成第二次 transition：

```cpp
barrier.setImage(image)
       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
       .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
       .setSrcAccessMask((vk::AccessFlagBits::eTransferWrite))
       .setDstAccessMask((vk::AccessFlagBits::eShaderRead))
       .setSubresourceRange(range);
cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                       vk::PipelineStageFlagBits::eFragmentShader, 
                       {}, {}, nullptr, barrier);
```

这次 `pipelineBarrier(...)` 的后四个参数与前一次相同：

1. `dependencyFlags = {}`
2. memory barriers = `{}`
3. buffer memory barriers = `nullptr`
4. image memory barriers = `barrier`

真正变化的是前两个阶段参数，以及 `vk::ImageMemoryBarrier` 里的 layout 和 access mask。  
因为这里已经不再是“准备接收上传数据”，而是“上传完成后准备给 fragment shader 读取”。

这一阶段的 `vk::ImageMemoryBarrier` 也可以按字段拆开看：

1. `setImage(image)`
   - 仍然是同一张纹理 image
2. `setOldLayout(vk::ImageLayout::eTransferDstOptimal)`
   - 旧状态是拷贝目标 layout
3. `setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)`
   - 新状态是 shader 只读采样 layout
4. `setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)`
   - barrier 之前必须先完成 transfer write
5. `setDstAccessMask(vk::AccessFlagBits::eShaderRead)`
   - barrier 之后允许 fragment shader 进行 shader read
6. `setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)`
   - 不处理队列家族所有权转移
7. `setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)`
   - 同样忽略队列家族所有权转移
8. `setSubresourceRange(range)`
   - 仍然只覆盖这张 2D 颜色纹理的 mip 0、layer 0

和第一次 transition 相比，这里多出来的关键项是：

```cpp
setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
```

因为这一次不再是“旧内容无意义”的 `eUndefined` 场景，而是刚刚执行过：

```cpp
copyBufferToImage(...)
```

所以需要明确声明：先等前面的 transfer write 完成，再允许后面的 shader read 开始。

这组 barrier 的含义是：

1. 源阶段是 `transfer`
2. 源访问是 `transfer write`
3. 目标阶段是 `fragment shader`
4. 目标访问是 `shader read`

于是数据依赖关系就被表达成：

```text
先完成 copy 写入
    -> 再允许 fragment shader 读取
```

这里要特别注意，layout transition 不只是“改一个枚举值”，它同时也在资源访问顺序上承担同步语义。

---

### 1.2.9 创建 `vk::ImageView`，把纹理 image 暴露成可绑定的 2D 颜色视图

当前项目的 `createImageView()` 如下：

```cpp
vk::ImageSubresourceRange range;
range.setAspectMask(vk::ImageAspectFlagBits::eColor)
     .setBaseArrayLayer(0)
     .setLayerCount(1)
     .setLevelCount(1)
     .setBaseMipLevel(0);
createInfo.setImage(image)
          .setViewType(vk::ImageViewType::e2D)
          .setComponents(mapping)
          .setFormat(vk::Format::eR8G8B8A8Srgb)
          .setSubresourceRange(range);
view = Context::Instance().device.createImageView(createInfo);
```

这里定义出来的是：

```text
一张 2D 的颜色纹理视图
    -> 格式为 R8G8B8A8_SRGB
    -> 只看 mip 0
    -> 只看 array layer 0
```

descriptor 中真正引用的也是这个 `view`，而不是 `image` 本体。

---

### 1.2.10 创建 `vk::Sampler`，把过滤、寻址和坐标解释方式固定下来

当前项目在 `Material::createSampler()` 中创建采样器：

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
          .setMipmapMode(vk::SamplerMipmapMode::eLinear);
sampler = Context::Instance().device.createSampler(createInfo);
```

这一步固定了 shader 采样时的若干关键行为：

1. 坐标使用归一化模式
2. 纹理坐标越界时按 repeat 平铺
3. 放大缩小都用线性过滤
4. 不进行 shadow compare
5. 当前不启用各向异性过滤

当前项目先把各向异性过滤关闭，是一种更朴素、更适合当前阶段的实现。  
如果后续要提高缩小采样或斜向采样质量，再补上 `samplerAnisotropy` 特性检查与相关 sampler 参数即可。

---

### 1.2.11 在 descriptor set 中写入 `CombinedImageSampler`

当前项目的 shader 资源布局，把纹理放在 `set = 1, binding = 0`：

```glsl
layout(set = 1, binding = 0) uniform sampler2D texSampler;
```

对应的 C++ 侧 descriptor set layout 是：

```cpp
vk::DescriptorSetLayoutBinding set1Binding;
set1Binding.setBinding(0)
    .setDescriptorCount(1)
    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
    .setStageFlags(vk::ShaderStageFlagBits::eFragment);
```

`stageFlags = eFragment` 这一点很重要。  
它明确表明当前 descriptor 会在 fragment shader 阶段使用。  
如果以后把纹理采样放到其它着色器阶段，对应的 `stageFlags` 也必须同步调整。

`Material` 为每个 in-flight frame 分配 descriptor set：

```cpp
std::vector layouts(flightCount, Context::Instance().shaderProgram->GetDescriptorSetLayouts()[1]);
allocInfo.setDescriptorPool(descriptorPool_)
         .setSetLayouts(layouts);
descriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);
```

真正写 descriptor 时，把 image view、sampler 和 image layout 组合到 `vk::DescriptorImageInfo`：

```cpp
vk::DescriptorImageInfo imageInfo;
imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
         .setImageView(texture ? texture->view : vk::ImageView{})
         .setSampler(sampler);

vk::WriteDescriptorSet writeInfo;
writeInfo.setImageInfo(imageInfo)
         .setDstBinding(0)
         .setDstArrayElement(0)
         .setDescriptorCount(1)
         .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
         .setDstSet(descriptorSets_[i]);

Context::Instance().device.updateDescriptorSets(writeInfo, {});
```

这一段把三件事绑定到了一起：

1. 哪个 image view 被读取
2. 读取时使用什么 sampler
3. shader 读取时假定图像处于什么 layout

---

### 1.2.12 Draw 时绑定材质 descriptor，然后在 fragment shader 中执行 `texture(...)`

当前项目在 `Renderer::Draw()` 中，先绑定 set 0 的动态 UBO，再绑定 set 1 的材质纹理：

```cpp
cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                       ctx.renderProcess->layout,
                       0,
                       mvpDescriptorSets_[curFrame_],
                       dynamicOffsets);
cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                       ctx.renderProcess->layout,
                       1,
                       material.GetDescriptorSet(curFrame_),
                       {});
```

fragment shader 最终执行采样：

```glsl
layout(location = 0) in vec2 Texcoord;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    vec4 color;
} pc;

void main() {
    outColor = texture(texSampler, Texcoord) * pc.color;
}
```

这里最后的颜色由两部分共同决定：

1. `texture(texSampler, Texcoord)`
   - 负责从纹理中取样
2. `* pc.color`
   - 负责对采样结果再做一层颜色调制

于是当前项目的 Texture mapping 不只是“显示原图”，而是“纹理采样结果 × 材质颜色”。

## 1.3 当前实现的关键特点

### 1.3.1 当前实现已经具备完整的 Vulkan 纹理采样链路

当前工程并不是只做了“加载图片”这一步，而是已经完整覆盖了：

1. CPU 读取图片
2. staging buffer 上传
3. `vk::Image` 创建与内存绑定
4. layout transition
5. `copyBufferToImage`
6. `vk::ImageView`
7. `vk::Sampler`
8. combined image sampler descriptor
9. shader 中的 `sampler2D` 采样

这条链路已经是 Vulkan 纹理系统最核心的基础版本。

---

### 1.3.2 当前实现还是基础版，后续还可以继续补强

当前项目仍然是一个基础但正确的实现，尚未覆盖：

1. mipmaps 生成
2. sampler anisotropy
3. 更复杂的材质贴图组合
4. 更复杂的 image subresource 视图 

## 1.4 小结

当前项目中的 Texture mapping，可以压缩成下面这条完整路径：

```text
Mesh 顶点写入 UV
    -> vertex input 把 texcoord 接到 location = 1
    -> vertex shader 输出 UV
    -> rasterizer 对 UV 插值
    -> stb_image 读取像素
    -> staging buffer 暂存像素
    -> 创建 device local 的 vk::Image
    -> eUndefined -> eTransferDstOptimal
    -> copyBufferToImage
    -> eTransferDstOptimal -> eShaderReadOnlyOptimal
    -> 创建 vk::ImageView
    -> 创建 vk::Sampler
    -> 通过 eCombinedImageSampler 写入 set = 1, binding = 0
    -> Draw 时绑定材质 descriptor set
    -> fragment shader 执行 texture(texSampler, Texcoord)
```

把这条链路拆开之后，Texture mapping 在 Vulkan 中就不再是一个模糊的“贴图功能”，而是一组明确的资源对象、布局状态、同步关系、描述符绑定和 shader 采样动作。
