# 1 Vulkan 中的模型加载

## 1.1 概念

### 1.1.1 模型加载在渲染系统中的作用

在 Vulkan 入门阶段，顶点数据通常直接写在代码里。  
这种方式适合讲清楚顶点缓冲、索引缓冲和绘制命令，但并不适合继续承载复杂场景。

一旦几何体来自建模工具，渲染系统要处理的问题就会变成：

1. 模型文件如何解析
2. 文件中的顶点属性如何恢复
3. 如何转成运行时的 `vertices + indices`
4. 如何把这批数据上传到 GPU

模型加载的核心价值，并不是“多支持一种文件格式”，而是让渲染系统的数据来源从“手写几何体”切换到“外部资源文件”。

---

### 1.1.2 OBJ 文件的顶点属性组织方式

OBJ 最容易让人误解的地方，是它看起来像在描述三角形，实际上它更像在描述几张“属性表”。

一个最小例子如下：

```obj
v  1 2 3
v  4 5 6
v  7 8 9

vt 0 0
vt 1 0
vt 1 1

f 1/1 2/2 3/3
```

这里有三类数据：

1. `v`
   - position 表
2. `vt`
   - texcoord 表
3. `f`
   - 面数据
   - 记录的不是最终顶点，而是“引用 position / texcoord 的索引”

因此，OBJ 文件中的面信息并不等价于项目里常见的：

```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec2 texcoord;
};
```

它们之间还隔着一步“属性恢复与拼装”。

---

### 1.1.3 tinyobjloader 的输出数据结构

`tinyobjloader` 读取 OBJ 后，最重要的三类输出是：

```cpp
tinyobj::attrib_t attrib;
std::vector<tinyobj::shape_t> shapes;
std::vector<tinyobj::material_t> materials;
```

它们各自的职责是：

1. `attrib`
   - 保存原始属性数组
   - 例如 `vertices`、`normals`、`texcoords`
2. `shapes`
   - 保存模型中的子网格 / 子对象 / 面集合
3. `materials`
   - 保存材质信息

当前实现主要使用：

1. `attrib.vertices`
2. `attrib.texcoords`
3. `shape.mesh.indices`

法线和材质拆分还没有接入，因此这一节的重点集中在“位置 + 纹理坐标”的恢复上。

---

### 1.1.4 `shapes` 的含义与作用

这是阅读 tinyobjloader 代码时最容易卡住的地方之一。

```cpp
std::vector<tinyobj::shape_t> shapes;
```

可以把 `shapes` 理解成：

> 一个 OBJ 文件拆分出来的多个子网格

一个 `.obj` 文件并不一定只包含一个整体模型，它可以包含：

1. 多个 object
2. 多个 group
3. 多段面数据

tinyobjloader 会把这些内容整理成多个 `shape_t`。  
因此下面这段遍历的含义就是“遍历文件中的每个子网格”：

```cpp
for (const auto& shape : shapes) {
    for (const auto& index : shape.mesh.indices) {
        ...
    }
}
```

其中：

```cpp
shape.mesh.indices
```

并不是顶点数组本身，而是“引用属性表的索引序列”。

---

### 1.1.5 扁平属性数组与偏移计算

这也是第二个最容易让人困惑的点。

`attrib.vertices` 的类型不是：

```cpp
std::vector<glm::vec3>
```

而是扁平存储的一维数组。它的逻辑形状更像：

```text
[x0, y0, z0, x1, y1, z1, x2, y2, z2, ...]
```

因此每个 position 占 3 个元素：

1. `x`
2. `y`
3. `z`

第 `index` 个顶点位置在数组中的起始下标自然就是：

```cpp
offset = index * 3;
```

读取方式也就变成：

```cpp
attrib.vertices[offset + 0] // x
attrib.vertices[offset + 1] // y
attrib.vertices[offset + 2] // z
```

同理，`texcoord` 数组是：

```text
[u0, v0, u1, v1, u2, v2, ...]
```

每个 UV 占 2 个元素，所以对应的是：

```cpp
offset = index * 2;
```

这类“扁平数组 + 手动计算起始偏移”的写法，在很多轻量模型加载器中都很常见。

---

### 1.1.6 扁平位置数组的索引示例

假设 OBJ 中有三组位置：

```obj
v 1 2 3
v 4 5 6
v 7 8 9
```

那么 `attrib.vertices` 可以理解为：

```cpp
attrib.vertices = {
    1, 2, 3,
    4, 5, 6,
    7, 8, 9
};
```

如果某个索引是：

```cpp
index.vertex_index = 1;
```

那么它引用的是第二个 position。  
计算方式如下：

```cpp
offset = 1 * 3 = 3;
```

于是：

```cpp
x = attrib.vertices[3]; // 4
y = attrib.vertices[4]; // 5
z = attrib.vertices[5]; // 6
```

最终恢复出的就是：

```cpp
{4, 5, 6}
```

把这一点想清楚之后，`ReadPosition(...)` 和 `ReadTexcoord(...)` 这类辅助函数就会顺很多。

---

### 1.1.7 运行时网格的目标数据结构

加载器真正要生成的，不是 OBJ 内部结构，而是渲染系统能直接消费的数据：

```cpp
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};
```

因此模型加载流程的本质可以概括为：

```text
OBJ 文件
    -> 属性表 + 面索引
    -> 恢复 position / texcoord
    -> 组装 Vertex
    -> 去重
    -> 生成 Mesh
```

这里最重要的一步不是“读文件”，而是“把文件中的索引系统翻译成运行时网格结构”。

---

### 1.1.8 顶点去重的必要性

如果每遍历一个 OBJ 索引就直接构造一个新顶点并 `push_back`，会出现大量重复数据。

这会带来两个问题：

1. 顶点缓冲被重复顶点撑大
2. 索引缓冲失去“复用顶点”的意义

因此更合理的做法是：

1. 先构造一个 `Vertex`
2. 查询它是否已经出现过
3. 如果没出现过，再加入 `vertices`
4. `indices` 永远记录“这个顶点在 `vertices` 里的编号”

当前实现使用：

```cpp
std::unordered_map<Vertex, std::uint32_t> uniqueVertices;
```

这一步完成后，索引缓冲才真正承担了“重复使用顶点”的职责。

---

### 1.1.9 `Vertex` 的相等比较与哈希

`unordered_map<Vertex, ...>` 能成立，前提是 C++ 知道两件事：

1. 怎样判断两个 `Vertex` 是否相同
2. 怎样计算 `Vertex` 的哈希值

因此需要为 `Vertex` 提供：

```cpp
bool operator==(const Vertex& other) const;
```

以及对应的：

```cpp
template<>
struct hash<toy2d::Vertex> { ... };
```

当前实现中，`Vertex` 的唯一性由：

1. `position`
2. `texcoord`

共同决定。  
这意味着“位置相同但 UV 不同”的两个顶点，仍然会被视为不同顶点。  
这正是纹理映射场景中正确的语义。

---

### 1.1.10 纹理坐标的纵向翻转

当前实现中恢复 UV 的逻辑如下：

```cpp
return {
    attrib.texcoords[offset + 0],
    1.0f - attrib.texcoords[offset + 1]
};
```

这里之所以对 `v` 做一次：

```cpp
1.0f - v
```

是因为 OBJ 的纹理坐标约定和当前纹理加载路径并不完全一致。  
与此同时，图片读取阶段又使用了：

```cpp
stbi_set_flip_vertically_on_load(true);
```

因此，模型 UV 和纹理图像的纵向方向必须对齐，否则贴图就会出现上下颠倒。

---

## 1.2 实现流程

### 1.2.1 模型加载接口的定义

运行时网格加载器对外只暴露一个很小的接口：

```cpp
Mesh LoadObjMesh(const std::filesystem::path& path);
```

这个接口的职责很单一：

1. 输入一个 OBJ 路径
2. 输出一个可以直接交给渲染器的 `Mesh`

保持接口简洁的好处是，OBJ 文件结构、路径解析和顶点去重这些细节都被封装在内部，不会泄漏到渲染层。

---

### 1.2.2 OBJ 文件解析与属性容器准备

加载的第一步，是让 tinyobjloader 把文件解析成属性表和子网格。

关键代码如下：

```cpp
tinyobj::attrib_t attrib;
std::vector<tinyobj::shape_t> shapes;
std::vector<tinyobj::material_t> materials;
std::string warning;
std::string error;

bool loaded = tinyobj::LoadObj(&attrib,
                               &shapes,
                               &materials,
                               &warning,
                               &error,
                               objectPath.string().c_str(),
                               materialDir.empty() ? nullptr : materialDir.c_str(),
                               true);
```

这里最后一个参数：

```cpp
true
```

表示让 tinyobjloader 自动三角化。  
这很重要，因为渲染管线当前按三角形列表工作，自动三角化可以减少后续处理复杂度。

如果加载失败，需要把文件路径和错误信息一起抛出：

```cpp
if (!loaded) {
    std::ostringstream message;
    message << "failed to load OBJ model: " << objectPath.string();
    if (!error.empty()) {
        message << " (" << error << ")";
    }
    throw std::runtime_error(message.str());
}
```

公开文章里把这一步写清楚很有必要，因为很多“模型加载失败”的问题，最终都不是 Vulkan，而是文件路径、`.mtl` 路径或文件本身的问题。

---

### 1.2.3 材质目录的解析与传递

OBJ 文件经常会包含：

```obj
mtllib xxx.mtl
```

因此加载时通常还要把材质目录传给 tinyobjloader：

```cpp
auto materialDir = objectPath.parent_path().string();
if (!materialDir.empty()) {
    materialDir += std::filesystem::path::preferred_separator;
}
```

然后在 `LoadObj(...)` 中作为 `base_dir` 传入。

即使当前实现还没有真正使用 `materials`，把目录处理正确依然是值得保留的，因为它保证了 OBJ 的外部引用关系是完整的。

---

### 1.2.4 `position` 与 `texcoord` 的恢复函数

把“从扁平属性数组里恢复单个属性”拆成独立函数，会让主流程清晰很多。

位置恢复函数如下：

```cpp
glm::vec3 ReadPosition(const tinyobj::attrib_t& attrib, int index) {
    auto offset = static_cast<std::size_t>(index) * 3;
    if (offset + 2 >= attrib.vertices.size()) {
        throw std::runtime_error("OBJ vertex position index is out of range");
    }

    return {
        attrib.vertices[offset + 0],
        attrib.vertices[offset + 1],
        attrib.vertices[offset + 2]
    };
}
```

这里最关键的是：

```cpp
index * 3
```

它对应的是 position 在扁平数组中的起始偏移。

纹理坐标恢复函数如下：

```cpp
glm::vec2 ReadTexcoord(const tinyobj::attrib_t& attrib, int index) {
    if (index < 0 || attrib.texcoords.empty()) {
        return {0.0f, 0.0f};
    }

    auto offset = static_cast<std::size_t>(index) * 2;
    if (offset + 1 >= attrib.texcoords.size()) {
        throw std::runtime_error("OBJ texture coordinate index is out of range");
    }

    return {
        attrib.texcoords[offset + 0],
        1.0f - attrib.texcoords[offset + 1]
    };
}
```

这里的：

```cpp
index * 2
```

对应的是 UV 在扁平数组中的起始偏移。

---

### 1.2.5 运行时顶点的构建

真正把 OBJ 数据转成运行时网格的核心，发生在这层双重循环中：

```cpp
for (const auto& shape : shapes) {
    for (const auto& index : shape.mesh.indices) {
        if (index.vertex_index < 0) {
            throw std::runtime_error("OBJ contains a face without vertex position");
        }

        Vertex vertex{};
        vertex.position = ReadPosition(attrib, index.vertex_index);
        vertex.texcoord = ReadTexcoord(attrib, index.texcoord_index);

        ...
    }
}
```

这段代码的含义可以直接翻译成：

```text
遍历每个子网格
    -> 遍历它的每个索引
    -> 从属性表中恢复 position / texcoord
    -> 拼成一个运行时 Vertex
```

从结构上看，这一步正是“文件格式”向“渲染数据结构”转换的交界点。

---

### 1.2.6 基于哈希表的顶点去重

拼出一个 `Vertex` 之后，并不立即把它压入顶点数组，而是先查询它是否已经存在：

```cpp
std::unordered_map<Vertex, std::uint32_t> uniqueVertices;
```

核心逻辑如下：

```cpp
auto [it, inserted] =
    uniqueVertices.try_emplace(vertex, static_cast<std::uint32_t>(mesh.vertices.size()));
if (inserted) {
    mesh.vertices.push_back(vertex);
}

mesh.indices.push_back(it->second);
```

这一段做了两件事：

1. 如果顶点第一次出现，就把它加入 `vertices`
2. 无论是否重复，都把对应编号压入 `indices`

于是最终得到的 `Mesh` 就是标准的：

```text
唯一顶点数组 + 索引数组
```

这一步是加载器从“能工作”变成“结构合理”的关键。

---

### 1.2.7 `Mesh` 的生成与渲染接入

模型加载器的职责到这里就结束了。  
后续流程重新回到渲染系统熟悉的路径：

```text
LoadObjMesh(...)
    -> 得到 Mesh
    -> Mesh::Upload()
    -> vertex buffer / index buffer
    -> DrawIndexed
```

入口代码类似：

```cpp
auto modelPath = toy2d::ResolveAssetPath("assets/models/viking_room.obj");
auto texturePath = toy2d::ResolveAssetPath("assets/textures/viking_room.png");

auto roomMesh = toy2d::LoadObjMesh(modelPath);
toy2d::Material roomMaterial;
roomMaterial.createTexture(texturePath.string());

toy2d::GameObject room;
room.SetMaterial(&roomMaterial);
room.SetMesh(&roomMesh);
room.SetPosition(glm::vec3{-0.08f, -0.01f, -0.40f});
room.SetRotationDegrees(glm::vec3{0.0f, 180.0f, 0.0f});
```

这里值得注意的是，模型加载和纹理加载在职责上仍然是分开的：

1. `LoadObjMesh(...)`
   - 负责几何数据
2. `createTexture(...)`
   - 负责图像资源

这两条路径最后在 `GameObject` 上汇合。

---

### 1.2.8 资源路径解析

如果程序不是从仓库根目录启动，直接写相对路径往往会失败。  
因此运行时还需要一层路径解析：

```cpp
std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath);
```

实现方式并不复杂：

1. 从当前工作目录开始尝试
2. 如果找不到，就逐级向父目录回退
3. 直到定位到真正的资源文件

关键实现如下：

```cpp
auto current = std::filesystem::current_path();
while (true) {
    auto candidate = current / relativePath;
    if (std::filesystem::exists(candidate)) {
        return std::filesystem::weakly_canonical(candidate);
    }

    auto parent = current.parent_path();
    if (parent == current) {
        break;
    }
    current = parent;
}
```

这一步虽然不属于 OBJ 格式本身，却是“博客示例能不能真的跑起来”的关键工程细节。

---

## 1.3 总结

模型加载这一节真正建立起来的，不只是一个 `LoadObjMesh(...)` 函数，而是一条完整的数据翻译链：

```text
OBJ 文件
    -> attrib / shapes / materials
    -> shape.mesh.indices 引用属性表
    -> 从扁平数组中恢复 position / texcoord
    -> 组装 Vertex
    -> 用哈希表去重
    -> 生成 Mesh{vertices, indices}
    -> 上传到 GPU
    -> 进入已有的 DrawIndexed 渲染路径
```

把这条链路看清楚之后，后续继续扩展：

1. 法线
2. 多材质
3. 切线 / 副切线
4. 更高效的 GPU 上传路径

都会变得顺很多。  
模型加载的难点并不在 Vulkan 命令本身，而在于如何把文件格式里的“属性索引系统”平稳地翻译成运行时网格。

---

参考教程：<https://vulkan-tutorial.com/Loading_models>
