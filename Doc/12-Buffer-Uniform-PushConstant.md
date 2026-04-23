# 1 VertexBuffer

## 1.1 概念

### 1.1.1 VertexBuffer 是什么

VertexBuffer 是一类专门给顶点输入阶段使用的 `vk::Buffer`。  
它最核心的职责不是“给 shader 提供任意可读资源”，而是“给图形管线的 Vertex Input 阶段提供顶点属性数据”。

当前项目会先为矩形顶点准备一块带有 `eVertexBuffer` 用途的 `Buffer`。`Buffer` 构造函数负责创建 Vulkan buffer、查询内存需求、分配内存并完成绑定；顶点坐标本身不会在构造时写入，而是在后续 `bufferVertexData()` 中通过 `WriteData` 写入。

```cpp
verticesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eVertexBuffer,
                                 sizeof(float) * 8,
                                 vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent));
```

绘制时，命令缓冲会把这块 buffer 绑定到 vertex input：

```cpp
vk::DeviceSize offset = 0;
cmd.bindVertexBuffers(0, verticesBuffer_->buffer, offset);
```

VertexBuffer 的数据路径可以概括为：

```text
创建 vk::Buffer + 绑定 vk::DeviceMemory
    -> WriteData 写入顶点数据
    -> bindVertexBuffers
    -> Vertex Input 阶段取数
    -> vertex shader 的 layout(location = N) in 变量
```

---

### 1.1.2 VertexBuffer 的输入位置

VertexBuffer 的语义是“顶点属性输入”。  
在当前项目里，顶点着色器接收的是：

```glsl
layout(location = 0) in vec2 inPosition;
```

`location = 0` 对应 pipeline 中的顶点属性描述。GPU 进入顶点阶段前，会根据 vertex input 描述从当前绑定的 VertexBuffer 中取出数据，并把解码后的值填入 `inPosition`。

这条路径使用的是 `location`、vertex binding description 和 vertex attribute description；UniformBuffer 使用的是 `set/binding` 与 DescriptorSet。两者都是把数据送进 shader，但入口机制并不相同。

---

### 1.1.3 VertexBuffer 与 `vk::Buffer`、`vk::DeviceMemory` 的关系

VertexBuffer 本质上并不是单独一种 Vulkan 对象，它仍然是：

```text
vk::Buffer + vk::DeviceMemory
```

只是它的 usage 会包含：

```cpp
vk::BufferUsageFlagBits::eVertexBuffer
```

当前项目中的 `Buffer` 封装如下：

```cpp
struct Buffer {
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    size_t size;
    size_t requireSize;

    void WriteData(const void* data, size_t dataSize, size_t offset = 0);

private:
    void* map_;
    vk::MemoryPropertyFlags memProperty_;
};
```

这几个成员分别承担不同职责：

1. `vk::Buffer`
   - 描述“这是一块拿来当 VertexBuffer 的资源对象”；
2. `vk::DeviceMemory`
   - 提供这块资源实际使用的内存；
3. `WriteData`
   - 对外提供统一的数据写入接口；
4. `map_`
   - 内部保存映射指针，外部不再直接访问。

---

### 1.1.4 当前项目里的 VertexBuffer 数据

当前项目用的是一个非常典型的二维矩形顶点数据：

```cpp
Vec vertices[] = {
    Vec{{-0.5, -0.5}},
    Vec{{0.5, -0.5}},
    Vec{{0.5, 0.5}},
    Vec{{-0.5, 0.5}},
};
verticesBuffer_->WriteData(vertices, sizeof(vertices));
```

VertexBuffer 保存的是“每个顶点一份”的属性数据，例如位置、法线、颜色、UV、切线等。

## 1.2 流程

### 1.2.1 创建 VertexBuffer 对应的 `vk::Buffer`

首先创建一块可以被图形管线当作顶点输入源使用的 buffer。

当前项目中的代码如下：

```cpp
verticesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eVertexBuffer,
                                 sizeof(float) * 8,
                                 vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent));
```

关键配置是 usage：

```cpp
vk::BufferUsageFlagBits::eVertexBuffer
```

这个标记告诉 Vulkan：这块 buffer 后续会作为 vertex input 使用。

---

### 1.2.2 为 VertexBuffer 分配并绑定 `vk::DeviceMemory`

`vk::Buffer` 只是资源句柄，真正可用前还需要分配并绑定 `vk::DeviceMemory`。

当前项目的 `Buffer` 构造函数里，这部分逻辑是统一的：

```cpp
buffer = device.createBuffer(createInfo);

auto requirements = device.getBufferMemoryRequirements(buffer);
auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);

vk::MemoryAllocateInfo allocInfo;
allocInfo.setMemoryTypeIndex(index)
         .setAllocationSize(requirements.size);

memory = device.allocateMemory(allocInfo);
device.bindBufferMemory(buffer, memory, 0);
```

这一段对应的流程是：

```text
createBuffer
    -> getBufferMemoryRequirements
    -> allocateMemory
    -> bindBufferMemory
```

---

### 1.2.3 写入顶点数据

创建和绑定内存之后，CPU 侧的顶点数组通过 `Buffer::WriteData` 写入映射内存。

外部只需要调用：

```cpp
verticesBuffer_->WriteData(vertices, sizeof(vertices));
```

`WriteData` 会检查这块内存是否已经映射、写入范围是否越界，然后用 `memcpy` 把数据拷贝到 mapped memory。当前实现如下：

```cpp
void Buffer::WriteData(const void* data, size_t dataSize, size_t offset) {
    if (!map_) {
        throw std::runtime_error("buffer memory is not host visible");
    }
    if (!data && dataSize > 0) {
        throw std::invalid_argument("buffer write data is null");
    }
    if (offset > size || dataSize > size - offset) {
        throw std::out_of_range("buffer write range is out of bounds");
    }

    std::memcpy(static_cast<char*>(map_) + offset, data, dataSize);
    flushIfNeeded(offset, dataSize);
}
```

`map_` 的类型是 `void*`，不能直接做指针加法；先转成 `char*` 后，`+ offset` 表示按字节偏移。写入完成后会调用 `flushIfNeeded`，由内部根据内存属性决定是否需要 `flushMappedMemoryRanges`。

---

### 1.2.4 在 Pipeline 中声明顶点格式

pipeline 还需要知道如何把这段线性内存解释成顶点属性。

当前项目在创建图形管线时：

```cpp
auto attributeDesc = Vec::GetAttributeDescription();
auto bindingDesc = Vec::GetBindingDescription();

vertexInputCreateInfo.setVertexAttributeDescriptions(attributeDesc)
                     .setVertexBindingDescriptions(bindingDesc);
```

这组描述会告诉 Vulkan：

1. 一个顶点的步长是多少；
2. 每个属性位于结构体哪个偏移；
3. shader 中的 `location` 对应哪段数据。

---

### 1.2.5 录制命令时绑定 VertexBuffer

录制绘制命令时，需要把当前使用的顶点数据源绑定到命令缓冲。

对应代码如下：

```cpp
vk::DeviceSize offset = 0;
cmd.bindVertexBuffers(0, verticesBuffer_->buffer, offset);
```

这行代码表示：vertex binding slot 0 使用 `verticesBuffer_` 作为输入源。

---

### 1.2.6 Vertex Shader 接收顶点属性

顶点着色器接收到的是 Vertex Input 阶段解码后的属性值。

当前项目的顶点着色器：

```glsl
layout(location = 0) in vec2 inPosition;
```

shader 看到的不是原始字节流，而是已经按照 vertex input 描述解析好的 `vec2`。VertexBuffer 这条链路可以总结为：

```text
vk::Buffer
    -> vk::DeviceMemory
    -> bindVertexBuffers
    -> Vertex Input
    -> layout(location = N) in
```

# 2 IndexBuffer

## 2.1 概念

### 2.1.1 IndexBuffer 是什么

IndexBuffer 是一类给索引装配阶段使用的 `vk::Buffer`。  
它的职责不是存放顶点属性本身，而是存放“顶点应该按什么顺序被取用”的索引数据。

在当前项目里，矩形实际上只有四个顶点，但要绘制两个三角形。  
使用 IndexBuffer 后，可以复用已有顶点：

```cpp
std::uint32_t indices[] = {
    0, 1, 3,
    1, 2, 3,
};
indicesBuffer_->WriteData(indices, sizeof(indices));
```

这组索引表达的是：

1. 第一个三角形使用顶点 `0, 1, 3`
2. 第二个三角形使用顶点 `1, 2, 3`

因此，IndexBuffer 的核心价值是：

1. 复用顶点数据；
2. 降低重复顶点带来的存储与传输开销；
3. 让图元连接关系由索引序列控制。

---

### 2.1.2 IndexBuffer 与 VertexBuffer 的关系

VertexBuffer 和 IndexBuffer 经常一起出现，但职责并不相同。

两者的关系可以概括为：

```text
VertexBuffer
    -> 回答“顶点属性是什么”

IndexBuffer
    -> 回答“顶点按什么顺序组成图元”
```

在当前项目里：

1. `verticesBuffer_` 存的是四个顶点的位置；
2. `indicesBuffer_` 存的是六个索引；
3. `drawIndexed(6, 1, 0, 0, 0)` 会按索引顺序组织两个三角形。

---

### 2.1.3 IndexBuffer 与 `vk::Buffer`、`vk::DeviceMemory` 的关系

和 VertexBuffer 一样，IndexBuffer 仍然是：

```text
vk::Buffer + vk::DeviceMemory
```

只不过它的 usage 会包含：

```cpp
vk::BufferUsageFlagBits::eIndexBuffer
```

当前项目里创建方式如下：

```cpp
indicesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eIndexBuffer,
                                sizeof(std::uint32_t) * 6,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent));
```

## 2.2 流程

### 2.2.1 创建 IndexBuffer 对应的 `vk::Buffer`

首先创建一块可供图形管线当作索引源使用的 buffer。

当前项目中的代码如下：

```cpp
indicesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eIndexBuffer,
                                sizeof(std::uint32_t) * 6,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent));
```

关键 usage 是：

```cpp
vk::BufferUsageFlagBits::eIndexBuffer
```

---

### 2.2.2 为 IndexBuffer 分配并绑定 `vk::DeviceMemory`

IndexBuffer 同样需要分配并绑定实际的 `vk::DeviceMemory`。

这部分流程与项目中的其它 Buffer 完全一致：

```cpp
buffer = device.createBuffer(createInfo);

auto requirements = device.getBufferMemoryRequirements(buffer);
auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);

vk::MemoryAllocateInfo allocInfo;
allocInfo.setMemoryTypeIndex(index)
         .setAllocationSize(requirements.size);

memory = device.allocateMemory(allocInfo);
device.bindBufferMemory(buffer, memory, 0);
```

---

### 2.2.3 写入索引数据

索引数组同样通过 `WriteData` 写进已经映射好的 IndexBuffer 内存。

代码如下：

```cpp
std::uint32_t indices[] = {
    0, 1, 3,
    1, 2, 3,
};
indicesBuffer_->WriteData(indices, sizeof(indices));
```

---

### 2.2.4 录制命令时绑定 IndexBuffer

录制绘制命令时，需要把索引源绑定到命令缓冲。

对应代码如下：

```cpp
cmd.bindIndexBuffer(indicesBuffer_->buffer, 0, vk::IndexType::eUint32);
```

三个参数分别表示：

1. 使用哪块 buffer 作为 index buffer；
2. 从哪个 offset 开始读取；
3. 索引类型是什么，这里是 `uint32_t`。

---

### 2.2.5 用 `drawIndexed` 触发索引绘制

绑定 IndexBuffer 后，`drawIndexed` 会按照索引顺序复用 VertexBuffer 里的顶点。

当前项目最终调用：

```cpp
cmd.drawIndexed(6, 1, 0, 0, 0);
```

当前项目的完整数据路径可以写成：

```text
VertexBuffer 提供顶点属性
    + IndexBuffer 提供索引顺序
    -> bindVertexBuffers
    -> bindIndexBuffer
    -> drawIndexed
    -> GPU 复用顶点并组装图元
```

# 3 UniformBuffer

## 3.1 概念

### 3.1.1 UniformBuffer 是什么

UniformBuffer 本质上也是 `vk::Buffer`，但它承担的是“给 shader 提供一块结构稳定、适合按帧或按批次共享的数据”的职责。

在当前项目里，UniformBuffer 主要承载两类数据：

1. 顶点着色器读取的 `project/view/model`：

```glsl
layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 project;
    mat4 view;
    mat4 model;
} ubo;
```

2. 片元着色器读取的颜色：

```glsl
layout(set = 1, binding = 0) uniform UniformBuffer {
    vec3 color;
} ubo;
```

和 VertexBuffer 最大的区别在于：  
UniformBuffer 是 **shader 主动通过 `set/binding` 接口访问的资源**。

---

### 3.1.2 为什么 UniformBuffer 需要 DescriptorSet

UniformBuffer 是当前几类数据入口里，唯一明确走 shader 资源绑定接口的一类。  
它的读取方式是：

```glsl
layout(set = X, binding = Y) uniform ...
```

这行声明已经给出了三层信息：

1. 资源位于哪个 descriptor set；
2. 资源位于该 set 的哪个 binding；
3. 这个 binding 的 descriptor 类型是 uniform buffer。

C++ 侧随后要把这份声明落到具体资源上，核心是两个问题：

1. 这个 `set/binding` 最终对应哪块 buffer？
2. 这个 buffer 的哪一段范围会暴露给 shader？

这两个问题由 `vk::DescriptorSet` 和 `vk::WriteDescriptorSet` 建立具体绑定关系。

因此，UniformBuffer 通常都会与以下对象一起出现：

1. `vk::DescriptorSetLayout`
2. `vk::DescriptorPool`
3. `vk::DescriptorSet`
4. `vk::WriteDescriptorSet`

---

### 3.1.3 `set` 和 `binding` 是什么

UniformBuffer 在 shader 里通常通过：

```glsl
layout(set = X, binding = Y) uniform ...
```

来声明资源入口。其中 `set` 和 `binding` 可以理解成 shader 资源绑定坐标：

```text
set
    -> 第几张资源表

binding
    -> 这张资源表里的第几个槽位
```

`set` 用来选择一组 descriptor 资源，`binding` 用来选择这组资源里的具体条目。

当前项目里有两组 uniform：

```glsl
layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 project;
    mat4 view;
    mat4 model;
} ubo;
```

这表示：

```text
set 0
    binding 0 -> MVP uniform buffer
```

片元着色器中的颜色 uniform：

```glsl
layout(set = 1, binding = 0) uniform UniformBuffer {
    vec3 color;
} ubo;
```

表示：

```text
set 1
    binding 0 -> Color uniform buffer
```

这两个声明会影响 C++ 侧的四个地方：

1. `DescriptorSetLayout` 的顺序要和 shader 的 `set` 对应；
2. `DescriptorSetLayoutBinding::binding` 要和 shader 的 `binding` 对应；
3. `updateDescriptorSets` 写入的 `dstBinding` 要和 shader 的 `binding` 对应；
4. `bindDescriptorSets` 的 `firstSet` 要和 shader 的 `set` 对应。

当前项目里 MVP 的绑定关系可以写成：

```text
shader:
layout(set = 0, binding = 0) uniform MVP

C++:
DescriptorSetLayout[0]
    binding 0 -> eUniformBuffer

DescriptorSet:
    mvpDescriptorSets_[curFrame_]

bind:
    bindDescriptorSets(..., firstSet = 0, mvpDescriptorSets_[curFrame_])
```

Color 的绑定关系则是：

```text
shader:
layout(set = 1, binding = 0) uniform Color

C++:
DescriptorSetLayout[1]
    binding 0 -> eUniformBuffer

DescriptorSet:
    colorDescriptorSets_[curFrame_]

bind:
    bindDescriptorSets(..., firstSet = 1, colorDescriptorSets_[curFrame_])
```

这就是 `set` 和 `binding` 的核心作用：  
**它们把 shader 侧的资源声明，定位到 C++ 侧具体的 descriptor set 和 binding 槽位上。**

---

### 3.1.4 UniformBuffer 与 `vk::Buffer`、`vk::DeviceMemory` 的关系

UniformBuffer 在资源层面依旧是：

```text
vk::Buffer + vk::DeviceMemory
```

只不过它的 usage 里必须包含：

```cpp
vk::BufferUsageFlagBits::eUniformBuffer
```

当前项目里还额外做了一个分层：

1. host visible 的 staging uniform buffer；
2. device local 的真正 uniform buffer。

当前项目没有让 shader 直接读取 CPU 可见内存，而是采用下面的路径：

```text
CPU 写 staging buffer
    -> copyBuffer
    -> device local uniform buffer
    -> descriptor 绑定给 shader
```

---

### 3.1.5 `vk::DescriptorPool` 是什么

`vk::DescriptorPool` 是 descriptor set 的分配池。  
有了它，后续才能批量分配 `vk::DescriptorSet`。

当前项目只使用 uniform buffer 类型的 descriptor，因此 pool 的配置比较简单：

```cpp
vk::DescriptorPoolSize size;
size.setDescriptorCount(flightCount * 2)
    .setType(vk::DescriptorType::eUniformBuffer);

vk::DescriptorPoolCreateInfo createInfo;
createInfo.setPoolSizes(size)
          .setMaxSets(flightCount * 2);
```

---

### 3.1.6 `vk::DescriptorSet` 是什么

`vk::DescriptorSet` 可以理解成“某一套 shader 资源布局的具体实例”。

它不保存 buffer 本体，而是保存“哪个 binding 对应哪个 buffer / image / sampler”的绑定关系。

当前项目里：

```cpp
std::vector<vk::DescriptorSet> mvpDescriptorSets_;
std::vector<vk::DescriptorSet> colorDescriptorSets_;
```

它们按 in-flight frame 分开，分别对应每帧自己的 uniform buffer。

## 3.2 流程

### 3.2.1 在 shader 中声明 UniformBuffer

UniformBuffer 的入口首先出现在 shader 资源声明中。

当前项目中，vertex shader 与 fragment shader 分别声明：

```glsl
layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 project;
    mat4 view;
    mat4 model;
} ubo;
```

```glsl
layout(set = 1, binding = 0) uniform UniformBuffer {
    vec3 color;
} ubo;
```

---

### 3.2.2 创建 `vk::DescriptorSetLayout`

C++ 侧需要用 `vk::DescriptorSetLayout` 描述 shader 中的 `set/binding`。

当前项目在 `Shader::initDescriptorSetLayouts()` 中创建两套 layout：

```cpp
vk::DescriptorSetLayoutBinding binding;
binding.setBinding(0)
       .setDescriptorCount(1)
       .setDescriptorType(vk::DescriptorType::eUniformBuffer)
       .setStageFlags(vk::ShaderStageFlagBits::eVertex);
createInfo.setBindings(binding);
layouts_.push_back(device.createDescriptorSetLayout(createInfo));

binding.setBinding(0)
       .setDescriptorCount(1)
       .setDescriptorType(vk::DescriptorType::eUniformBuffer)
       .setStageFlags(vk::ShaderStageFlagBits::eFragment);
createInfo.setBindings(binding);
layouts_.push_back(device.createDescriptorSetLayout(createInfo));
```

---

### 3.2.3 在 PipelineLayout 中登记 Uniform 对应的 set layouts

创建 pipeline layout 时，需要登记 shader 会使用的 descriptor set layout。

当前项目的 `RenderProcess::createLayout()`：

```cpp
vk::PipelineLayoutCreateInfo createInfo;
createInfo.setSetLayouts(Context::Instance().shader->GetDescriptorSetLayouts())
          .setPushConstantRanges({range});
```

---

### 3.2.4 创建 UniformBuffer 对应的 `vk::Buffer` 与 `vk::DeviceMemory`

接下来创建真正承载 uniform 数据的 buffer 资源。

当前项目的 `Renderer::createUniformBuffers()` 里，MVP 使用两类 buffer：

1. CPU 写入用：

```cpp
buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
             size,
             vk::MemoryPropertyFlagBits::eHostVisible |
             vk::MemoryPropertyFlagBits::eHostCoherent));
```

2. shader 读取用：

```cpp
buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferDst |
             vk::BufferUsageFlagBits::eUniformBuffer,
             size,
             vk::MemoryPropertyFlagBits::eDeviceLocal));
```

颜色 uniform 采用相同模式。

---

### 3.2.5 更新 UniformBuffer 的内容

每帧更新 uniform 时，CPU 先把新数据写入 staging buffer，再复制到 device local uniform buffer。

MVP 的更新方式：

```cpp
MVP mvp;
mvp.project = projectMat_;
mvp.view = viewMat_;
mvp.model = model;

buffer->WriteData(&mvp, sizeof(mvp));
transformBuffer2Device(*buffer, *deviceMvpUniformBuffers_[i], 0, 0, buffer->size);
```

颜色的更新方式：

```cpp
buffer->WriteData(&color, sizeof(float) * 3);
transformBuffer2Device(*buffer, *deviceColorUniformBuffers_[i], 0, 0, buffer->size);
```

---

### 3.2.6 创建 `vk::DescriptorPool`

DescriptorSet 需要从 DescriptorPool 中分配，因此要先创建 pool。

对应代码如下：

```cpp
vk::DescriptorPoolCreateInfo createInfo;
vk::DescriptorPoolSize size;
size.setDescriptorCount(flightCount * 2)
    .setType(vk::DescriptorType::eUniformBuffer);
createInfo.setPoolSizes(size)
          .setMaxSets(flightCount * 2);

descriptorPool_ = device.createDescriptorPool(createInfo);
```

---

### 3.2.7 分配 `vk::DescriptorSet`

有了 pool 之后，就可以分配可更新、可绑定的 descriptor set 实例。

当前项目先分配 MVP 那一组：

```cpp
std::vector layouts(flightCount, shader->GetDescriptorSetLayouts()[0]);
vk::DescriptorSetAllocateInfo allocInfo;
allocInfo.setDescriptorPool(descriptorPool_)
         .setSetLayouts(layouts);
mvpDescriptorSets_ = device.allocateDescriptorSets(allocInfo);
```

再分配颜色那一组：

```cpp
layouts = std::vector(flightCount, shader->GetDescriptorSetLayouts()[1]);
allocInfo.setDescriptorPool(descriptorPool_)
         .setSetLayouts(layouts);
colorDescriptorSets_ = device.allocateDescriptorSets(allocInfo);
```

---

### 3.2.8 用 `vk::WriteDescriptorSet` 把 UniformBuffer 写进 set

分配出来的 DescriptorSet 还只是空壳，需要用 `vk::WriteDescriptorSet` 把具体 buffer 写到指定 binding。

当前项目写 MVP：

```cpp
vk::DescriptorBufferInfo bufferInfo1;
bufferInfo1.setBuffer(deviceMvpUniformBuffers_[i]->buffer)
           .setOffset(0)
           .setRange(sizeof(float) * 4 * 4 * 3);

writeInfos[0].setBufferInfo(bufferInfo1)
             .setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eUniformBuffer)
             .setDescriptorCount(1)
             .setDstSet(mvpDescriptorSets_[i]);
```

写颜色：

```cpp
vk::DescriptorBufferInfo bufferInfo2;
bufferInfo2.setBuffer(deviceColorUniformBuffers_[i]->buffer)
           .setOffset(0)
           .setRange(sizeof(float) * 3);

writeInfos[1].setBufferInfo(bufferInfo2)
             .setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eUniformBuffer)
             .setDescriptorCount(1)
             .setDstSet(colorDescriptorSets_[i]);
```

最后提交：

```cpp
device.updateDescriptorSets(writeInfos, {});
```

---

### 3.2.9 绘制时绑定 Uniform 对应的 DescriptorSet

绘制前绑定本帧对应的 descriptor set，shader 才能通过 `set/binding` 找到对应的 uniform buffer。

对应代码如下：

```cpp
cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                       renderProcess->layout,
                       0, mvpDescriptorSets_[curFrame_], {});
cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                       renderProcess->layout,
                       1, colorDescriptorSets_[curFrame_], {});
```

UniformBuffer 这条链路可以概括成：

```text
vk::Buffer
    -> vk::DeviceMemory
    -> DescriptorSet
    -> PipelineLayout
    -> shader 中的 set / binding
```

# 4 PushConstant

## 4.1 概念

### 4.1.1 PushConstant 是什么

PushConstant 是 Vulkan 提供的一种“直接在命令录制阶段推送少量数据给 shader”的机制。

当前项目的顶点着色器中声明：

```glsl
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;
```

绘制前写入：

```cpp
cmd.pushConstants(renderProcess->layout,
                  vk::ShaderStageFlagBits::eVertex,
                  0,
                  sizeof(Mat4),
                  model.GetData());
```

PushConstant 适合的数据特点是：

1. 体积小；
2. 更新频繁；
3. 常常按 draw call 变化；
4. 不值得专门走一套 DescriptorSet 流程。

---

### 4.1.2 PushConstant 和 UniformBuffer 的区别

两者都能把数据传给 shader，但它们的分工不同。

UniformBuffer 更适合：

1. per-frame 数据；
2. per-material 数据；
3. 结构相对稳定的数据块；
4. 需要通过 descriptor 统一管理的数据。

PushConstant 更适合：

1. per-draw 数据；
2. 体积非常小的数据；
3. 每次绘制前都可能变化的数据。

当前项目采用的分工是：

1. `project`、`view`、颜色这些较稳定的数据放进 UniformBuffer；
2. 每个矩形都不同的 `model` 放进 PushConstant。

---

### 4.1.3 PushConstant 与 PipelineLayout 的关系

PushConstant 虽然不依赖 DescriptorSet，但它仍然强依赖 `vk::PipelineLayout`。

原因是 pipeline layout 会登记这段 push constant 的：

1. offset；
2. size；
3. stage flags。

## 4.2 流程

### 4.2.1 在 shader 中声明 PushConstant

PushConstant 的入口同样先写在 shader 中。

对应代码如下：

```glsl
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;
```

---

### 4.2.2 创建 `vk::PushConstantRange`

C++ 侧通过 `vk::PushConstantRange` 声明这段 push constant 的可见范围。

当前项目在 `Shader::GetPushConstantRange()` 中：

```cpp
vk::PushConstantRange range;
range.setOffset(0)
     .setSize(sizeof(Mat4))
     .setStageFlags(vk::ShaderStageFlagBits::eVertex);
```

---

### 4.2.3 在 PipelineLayout 中登记 PushConstantRange

PipelineLayout 需要登记这段 push constant range。

对应代码如下：

```cpp
vk::PipelineLayoutCreateInfo createInfo;
auto range = Context::Instance().shader->GetPushConstantRange();
createInfo.setSetLayouts(Context::Instance().shader->GetDescriptorSetLayouts())
          .setPushConstantRanges({range});
```

---

### 4.2.4 绘制前计算当前对象的模型矩阵

每个 draw call 都可以计算自己的对象级变换数据。

当前项目每次绘制矩形时：

```cpp
auto model = Mat4::CreateTranslate(rect.position).Mul(Mat4::CreateScale(rect.size));
```

---

### 4.2.5 用 `cmd.pushConstants(...)` 在录制命令时推送数据

绘制前调用 `cmd.pushConstants(...)`，把当前 draw 所需的小块数据写入 command buffer 状态。

对应代码如下：

```cpp
cmd.pushConstants(renderProcess->layout,
                  vk::ShaderStageFlagBits::eVertex,
                  0,
                  sizeof(Mat4),
                  model.GetData());
```

---

### 4.2.6 PushConstant 的完整链路

当前项目里的 PushConstant 数据流如下：



```text
shader 声明 layout(push_constant)
    -> C++ 创建 PushConstantRange
    -> PipelineLayout 登记该 range
    -> 每次 DrawRect 计算 model
    -> cmd.pushConstants(...)
    -> vertex shader 读取 pc.model
```

# 5 MemoryPropertyFlags：eHostVisible、eHostCoherent、eDeviceLocal

## 5.1 概念

### 5.1.1 这三个标记描述的是内存类型特性

在 Vulkan 里，应用并不是直接指定“把资源放进哪一块固定内存区域”，而是先创建 `vk::Buffer` 或 `vk::Image`，再从资源允许使用的 memory type 中，选择满足条件的一种。

当前项目里的查询逻辑就是：

```cpp
auto requirements = device.getBufferMemoryRequirements(buffer);
auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);
```

`memProperty` 可以是：

```cpp
vk::MemoryPropertyFlagBits::eHostVisible |
vk::MemoryPropertyFlagBits::eHostCoherent
```

或者：

```cpp
vk::MemoryPropertyFlagBits::eDeviceLocal
```

它表达的是：

```text
从资源允许使用的 memory type 中
    -> 选择满足这些属性的一类
```

因此，`eHostVisible`、`eHostCoherent`、`eDeviceLocal` 描述的是 **内存类型的访问特性**，不是硬编码的某一块固定显存名称。

---

### 5.1.2 为什么创建 Buffer 时已经指定了 `size`，还要调用 `getBufferMemoryRequirements`

`vk::BufferCreateInfo::size` 表示的是这块 Buffer 的**逻辑大小**。  
它回答的问题是：

```text
这个 Buffer 对外看起来需要容纳多少数据
```

例如当前项目里创建 VertexBuffer、IndexBuffer 或 UniformBuffer 时，传入的 `size` 都是在表达：

1. 顶点数据一共要占多少字节；
2. 索引数据一共要占多少字节；
3. uniform 数据结构一共要占多少字节。

但 Vulkan 的资源创建和内存分配是分开的。  
因此，“逻辑大小”并不等于“实际分配时必须满足的真实内存要求”。

这正是还要调用：

```cpp
auto requirements = device.getBufferMemoryRequirements(buffer);
```

的原因。这个查询返回的 `vk::MemoryRequirements` 至少包含三类关键信息：

1. `requirements.size`
   - 真正至少要分配多大的内存；
2. `requirements.alignment`
   - 内存绑定时必须满足什么对齐要求；
3. `requirements.memoryTypeBits`
   - 这块资源允许使用哪些 memory type。

两者的分工是：

```text
createBuffer(size)
    -> 决定资源逻辑大小

getBufferMemoryRequirements(buffer)
    -> 决定资源真实分配要求
```

这两步并不是重复，而是 Vulkan 显式内存模型里两个不同层面的信息。

举一个更直接的理解方式：

1. 创建 Buffer 时写的 `size`
   - 像是在说“这个容器逻辑上要装多少数据”；
2. `requirements.size`
   - 像是在说“驱动和硬件要求这块资源至少按多大粒度分配”。

在某些平台上，这两个值可能刚好一样；  
但这只是碰巧相等，不能当作规则依赖。驱动完全可能因为对齐、实现细节或资源约束，要求：

```text
requirements.size >= createInfo.size
```

当前项目采用下面的写法，既拿到了真实分配大小，也拿到了合法的 memory type 范围：

```cpp
auto requirements = device.getBufferMemoryRequirements(buffer);
auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);

vk::MemoryAllocateInfo allocInfo;
allocInfo.setMemoryTypeIndex(index)
         .setAllocationSize(requirements.size);
```

如果只拿创建时传进去的 `size` 去分配，而不查询 `requirements`，就可能出现三类问题：

1. 分配大小不足；
2. 忽略硬件要求的对齐限制；
3. 不知道这块资源允许绑定到哪些 memory type。

更准确的理解是：

```text
createInfo.size
    -> Buffer 的逻辑数据大小

requirements
    -> Buffer 的真实内存分配约束
```

另一个容易混淆的问题是：为什么 `queryBufferMemTypeIndex` 的第一个参数必须来自 `requirements.memoryTypeBits`，而不能随便传一个值？

原因是 `queryBufferMemTypeIndex` 的两个参数语义完全不同：

1. 第一个参数：`requirements.memoryTypeBits`
   - 表示 **当前这个 Buffer 允许使用哪些 memory type**
   - 这是资源本身的合法性约束
2. 第二个参数：`memProperty`
   - 表示 **应用希望这块内存具备哪些属性**
   - 例如 `eHostVisible | eHostCoherent` 或 `eDeviceLocal`

这个查询函数本质上是在做两层筛选：

```text
当前资源允许使用的 memory type
    ∩
应用希望具备的内存属性
    =
最终可用的 memoryTypeIndex
```

如果不从 `requirements.memoryTypeBits` 里拿第一个参数，而只按 `memProperty` 去找，就可能出现这种情况：

1. 某个 memory type 的属性看起来满足要求；
2. 但当前这个 Buffer 实际并不允许绑定到这种 memory type；
3. 最终就会选出一个“属性上看着对、但资源实际上不能用”的 index。

两者的分工是：

```text
requirements.memoryTypeBits
    -> 解决“这块 Buffer 允许用哪些内存类型”

memProperty
    -> 解决“应用希望这块内存具备什么访问特性”
```

两者缺一不可。  
所以当前项目必须同时传入 `requirements.memoryTypeBits` 和期望的 `memProperty`：

```cpp
auto requirements = device.getBufferMemoryRequirements(buffer);
auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);
```

---

### 5.1.3 `eHostVisible`：CPU 可以映射和访问

`eHostVisible` 的核心含义是：  
这类内存允许 Host，也就是 CPU 侧，通过 `mapMemory` 建立映射。

当前项目中的 `Buffer` 封装就直接体现了这一点：

```cpp
if (memProperty & vk::MemoryPropertyFlagBits::eHostVisible) {
    map_ = device.mapMemory(memory, 0, requireSize);
} else {
    map_ = nullptr;
}
```

有了 `eHostVisible` 之后，CPU 就可以直接：

```cpp
buffer.WriteData(data, size);
```

因此它常常适合：

1. staging buffer；
2. 教学阶段直接由 CPU 更新的数据；
3. 高频小数据上传场景。

它强调的是 **CPU 访问方便**，不自动等于“GPU 访问也最优”。

---

### 5.1.4 `eHostCoherent`：Host 与 Device 的可见性处理更省事

`eHostCoherent` 的核心含义是：  
Host 侧缓存与设备可见内存之间的同步步骤更简单。

如果一块内存同时具有：

```cpp
vk::MemoryPropertyFlagBits::eHostVisible |
vk::MemoryPropertyFlagBits::eHostCoherent
```

那么通常可以直接：

```cpp
buffer.WriteData(data, size);
```

而不需要额外手动调用：

1. `flushMappedMemoryRanges`
2. `invalidateMappedMemoryRanges`

这也是当前项目在 VertexBuffer、IndexBuffer 以及 staging uniform buffer 上选择这组属性的重要原因：  
CPU 写入路径最直观，代码最适合学习。

需要特别区分的是，`eHostCoherent` 只简化 **缓存可见性**，不替代执行同步。  
它不负责解决：

1. GPU 是否已经执行完成；
2. 某次 copy / draw 是否已经结束；
3. 不同阶段之间的先后顺序。

这些仍然要靠 fence、semaphore、barrier 等机制处理。

---

### 5.1.5 `eDeviceLocal`：更适合设备侧高效访问

`eDeviceLocal` 的核心含义是：  
这类内存对设备，也就是 GPU 侧访问更本地、更高效。

在独立显卡上，它通常更接近“本地显存”的语义；  
在集成显卡或统一内存架构上，它不一定是一块物理上独立的显存，但驱动仍会把它标记为“对 GPU 更合适”的内存类型。

因此，`eDeviceLocal` 更适合承载：

1. GPU 高频读取的 vertex / index / uniform 数据；
2. 纹理与 image 资源；
3. attachment、depth 等长期驻留的渲染资源。

它强调的是 **GPU 访问更优**，不保证一定可以被 Host 直接映射。

## 5.2 流程

### 5.2.1 `eHostVisible | eHostCoherent` 的典型使用方式

`eHostVisible | eHostCoherent` 很适合演示 CPU 直接写 buffer 的路径。

当前项目中，VertexBuffer 的创建方式就是：

```cpp
verticesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eVertexBuffer,
                                 sizeof(float) * 8,
                                 vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent));
```

IndexBuffer 也是同样的思路：

```cpp
indicesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eIndexBuffer,
                                sizeof(std::uint32_t) * 6,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent));
```

后续数据更新就可以直接：

```cpp
verticesBuffer_->WriteData(vertices, sizeof(vertices));
indicesBuffer_->WriteData(indices, sizeof(indices));
```

这条路径的特点是：

```text
CPU 易于映射
    -> CPU 易于写入
    -> coherent 让缓存可见性处理更简单
    -> 适合教学或上传型资源
```

---

### 5.2.2 只有 `eHostVisible` 而没有 `eHostCoherent` 时要做什么

如果 host visible memory 不是 coherent，就需要额外处理缓存可见性。

如果一块内存只有：

```cpp
vk::MemoryPropertyFlagBits::eHostVisible
```

但不包含：

```cpp
vk::MemoryPropertyFlagBits::eHostCoherent
```

CPU 与设备之间的缓存可见性可能不会自动同步，因此需要区分两种方向：

1. **Host 写完给 Device 看**：调用 `vkFlushMappedMemoryRanges`；
2. **Device 写完给 Host 看**：调用 `vkInvalidateMappedMemoryRanges`。

当前项目里的 `WriteData` 只覆盖“Host 写、Device 读”的上传路径，所以封装了 `flushIfNeeded`。如果内存带有 `eHostCoherent`，函数直接返回；否则会按 `nonCoherentAtomSize` 扩大 flush 范围，再调用 `flushMappedMemoryRanges`：

```cpp
void Buffer::flushIfNeeded(size_t offset, size_t dataSize) {
    if (dataSize == 0 ||
        (memProperty_ & vk::MemoryPropertyFlagBits::eHostCoherent)) {
        return;
    }

    auto& ctx = Context::Instance();
    vk::DeviceSize atomSize = ctx.phyDevice.getProperties().limits.nonCoherentAtomSize;
    vk::DeviceSize flushOffset = static_cast<vk::DeviceSize>(offset);
    vk::DeviceSize flushEnd = static_cast<vk::DeviceSize>(offset + dataSize);

    flushOffset = (flushOffset / atomSize) * atomSize;
    flushEnd = ((flushEnd + atomSize - 1) / atomSize) * atomSize;
    if (flushEnd > requireSize) {
        flushEnd = requireSize;
    }

    vk::MappedMemoryRange range;
    range.setMemory(memory)
         .setOffset(flushOffset)
         .setSize(flushEnd - flushOffset);
    ctx.device.flushMappedMemoryRanges(range);
}
```

如果以后需要从 GPU 写入的 mapped memory 中读取数据，则要在 CPU 读取前调用 `invalidateMappedMemoryRanges`。两者可以记成：

```text
Host write -> Flush -> Device read
Device write -> Invalidate -> Host read
```

---

### 5.2.3 `eDeviceLocal` 的典型使用方式

`eDeviceLocal` 通常会和 staging buffer 配合使用。

当前项目中的 uniform 采用了两层 buffer：

1. CPU 写入层：

```cpp
buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
             size,
             vk::MemoryPropertyFlagBits::eHostVisible |
             vk::MemoryPropertyFlagBits::eHostCoherent));
```

2. GPU 读取层：

```cpp
buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferDst |
             vk::BufferUsageFlagBits::eUniformBuffer,
             size,
             vk::MemoryPropertyFlagBits::eDeviceLocal));
```

后续通过：

```cpp
transformBuffer2Device(*buffer, *deviceMvpUniformBuffers_[i], 0, 0, buffer->size);
```

把 staging buffer 中的数据复制到 device local uniform buffer。

这是 Vulkan 中很常见的上传设计：

```text
HostVisible staging
    -> 方便 CPU 写
    -> copyBuffer
    -> DeviceLocal 正式资源
    -> 方便 GPU 高效读取
```

这样可以同时兼顾：

1. CPU 上传方便；
2. GPU 访问高效。

---

### 5.2.4 在当前项目里如何理解这三者的分工

当前工程中的内存属性选择可以归纳为两类。

当前项目可以概括为两类思路：

1. **直接 CPU 写入型**
   - VertexBuffer
   - IndexBuffer
   - staging uniform buffer
   - 使用 `eHostVisible | eHostCoherent`
2. **最终 GPU 读取型**
   - device local uniform buffer
   - 使用 `eDeviceLocal`

三者的分工可以简化为：

```text
eHostVisible
    -> 允许 map

eHostCoherent
    -> 让 Host/Device 可见性处理更省事

eDeviceLocal
    -> 让 GPU 访问更适合长期读取
```

## 5.3 实践中的几个关键点

### 5.3.1 `eHostVisible` 不等于“GPU 访问也最快”

`eHostVisible` 强调的是 CPU 可访问。  
它常常适合上传和动态更新，但不一定是 GPU 最喜欢的长期驻留区域。

### 5.3.2 `eHostCoherent` 不等于“完全不用同步”

它只是在缓存可见性上帮应用省步骤。  
它不替代 fence、semaphore、queue wait、barrier 这些执行同步机制。

### 5.3.3 `eDeviceLocal` 不一定绝对不可映射

在一些统一内存架构上，某个 memory type 可能同时具有：

1. `eDeviceLocal`
2. `eHostVisible`

因此更准确的理解应该是：

```text
eDeviceLocal
    -> 表达“对设备访问更优”
    -> 不等于“一定无法 map”
```

### 5.3.4 non-coherent memory 还要注意同步粒度

如果 HostVisible 内存不是 HostCoherent，那么除了要调用 flush / invalidate，还要注意 offset 与 size 通常需要满足设备的 non-coherent 粒度要求。  
这一点在工程进一步优化时会非常重要。

# 6 小结

按资源路径与内存属性划分，当前项目可以拆成几条主线。

## 6.1 VertexBuffer 这条线

```text
vk::Buffer
    -> vk::DeviceMemory
    -> bindVertexBuffers
    -> Vertex Input
    -> shader 的 layout(location = N) in
```

特点：

1. 面向顶点属性；
2. 依赖顶点输入描述和 `location`；
3. 在当前项目中使用 `eHostVisible | eHostCoherent` 方便 CPU 直接写入。

## 6.2 IndexBuffer 这条线

```text
vk::Buffer
    -> vk::DeviceMemory
    -> bindIndexBuffer
    -> drawIndexed
    -> 控制顶点复用与图元装配顺序
```

特点：

1. 面向索引组织；
2. 与 VertexBuffer 配合使用；
3. 在当前项目中同样使用 `eHostVisible | eHostCoherent` 方便 CPU 直接写入。

## 6.3 UniformBuffer 这条线

```text
staging buffer
    -> HostVisible / HostCoherent
    -> copyBuffer
    -> device local uniform buffer
    -> DescriptorSet
    -> shader 的 layout(set, binding) uniform
```

特点：

1. 面向 shader 资源读取；
2. 依赖 Descriptor 体系；
3. 当前项目通过 staging + `eDeviceLocal` 的方式兼顾 CPU 上传与 GPU 读取效率。

## 6.4 PushConstant 这条线

```text
CPU 小块数据
    -> PushConstantRange
    -> PipelineLayout
    -> cmd.pushConstants
    -> shader 的 layout(push_constant)
```

特点：

1. 面向 per-draw 小块数据；
2. 不需要单独分配 buffer；
3. 适合高频变化的小参数。

当前项目里的几类数据入口分别是：

1. 顶点坐标走 VertexBuffer；
2. 顶点复用顺序走 IndexBuffer；
3. MVP 与颜色这类共享参数走 UniformBuffer；
4. 每个矩形各自变化的模型矩阵走 PushConstant。

同时，内存属性上的分工也很明确：

1. `eHostVisible`
   - 解决 CPU 能不能映射和写入；
2. `eHostCoherent`
   - 解决 Host 与 Device 可见性步骤是否需要手动处理；
3. `eDeviceLocal`
   - 解决资源是否更适合长期停留在 GPU 高效访问的区域。

把这些数据路径与内存属性区分清楚之后，再看 descriptor、vertex input、index drawing、pipeline layout、staging、per-frame 资源管理时，就更容易判断每个对象到底服务于哪一条链路。

