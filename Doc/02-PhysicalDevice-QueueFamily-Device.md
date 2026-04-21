# 1 PhysicalDevice

## 1.1 概念

`vk::PhysicalDevice`（C API: `VkPhysicalDevice`）表示一块真实可用的 GPU。  
它本身不是“可提交命令的设备”，而是硬件能力的描述对象，用于回答三个核心问题：

1. 这块 GPU 是什么类型（独显/核显/虚拟设备）；
2. 它支持哪些队列能力（Graphics/Compute/Transfer/Present）；
3. 它支持哪些扩展与特性（例如 `VK_KHR_swapchain`）。

在初始化链路里，`PhysicalDevice` 的职责是“做硬件能力筛选”，不是直接执行渲染命令。真正执行命令的是后续创建出的 `vk::Device` 和 `vk::Queue`。

常见误区：

1. 只选第一块设备，不做能力校验；
2. 把“独显优先”当成“功能一定完整”；
3. 先创 `Device` 再查队列族（顺序应相反）。

## 1.2 流程

### 1.2.1 枚举可用 GPU：`vk::Instance::enumeratePhysicalDevices`

目标：拿到当前系统可见的所有 Vulkan 物理设备。

```cpp
auto devices = instance.enumeratePhysicalDevices();
if (devices.empty()) {
    throw std::runtime_error("No Vulkan physical devices found.");
}
```

### 1.2.2 制定设备选择策略：`vk::PhysicalDevice::getProperties`

目标：在多个设备中选出优先设备（项目当前采用“独显优先，否则回退第一块设备”的策略）。

```cpp
phyDevice = devices.front();
for (const auto& device : devices) {
    const auto properties = device.getProperties();
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        phyDevice = device;
        break;
    }
}
```

### 1.2.3 记录设备信息用于调试：`vk::PhysicalDeviceProperties`

目标：把最终选中的设备打印出来，便于定位驱动和平台问题。

```cpp
std::cout << phyDevice.getProperties().deviceName << std::endl;
```

# 2 QueueFamily

## 2.1 概念

Queue Family 可以理解成 GPU 的“执行通道分组”。  
每个 family 下可能有多个 queue（索引 0、1、2...），同一 family 内的 queue 能力集合相同。

在项目里，最关键的是两类能力：

1. **Graphics**：能执行图形管线命令；
2. **Present**：能把渲染结果提交到窗口 surface。

为什么要先找 Queue Family？  
因为 `vk::Device` 创建时必须声明“我要从哪些 queue family 申请 queue”，所以 family index 是 `createDevice` 的输入前提。

常见误区：

1. 认为 graphics family 一定支持 present（很多平台并不保证）；
2. 忘记传入 `surface` 做 present 支持查询；
3. family 找到了但没有保存 index，导致后续 `value()` 崩溃。

## 2.2 流程

### 2.2.1 读取队列族属性：`vk::PhysicalDevice::getQueueFamilyProperties`

目标：遍历所有 queue family，逐个判断能力。

```cpp
auto properties = phyDevice.getQueueFamilyProperties();
for (std::size_t i = 0; i < properties.size(); ++i) {
    const auto& property = properties[i];
    // ...
}
```

返回值类型是：

```cpp
std::vector<vk::QueueFamilyProperties>
```

也就是说，`properties[i]` 就是第 `i` 个 queue family 的能力描述。常用字段包括：

1. `queueFlags`：支持的能力位（`eGraphics / eCompute / eTransfer` 等）；
2. `queueCount`：该 family 里可用 queue 的数量；
3. `timestampValidBits`、`minImageTransferGranularity`：时间戳与拷贝粒度能力。

要特别注意：`vk::QueueFamilyProperties` **不包含 Present 支持信息**。  
Present 是否可用必须通过 `vk::PhysicalDevice::getSurfaceSupportKHR(i, surface)` 额外查询。

### 2.2.2 查找 Graphics 队列：`vk::QueueFlagBits::eGraphics`

目标：记录能跑图形命令的 family index。

```cpp
if (property.queueFlags & vk::QueueFlagBits::eGraphics) {
    queueFamilyIndices.graphicsQueue = static_cast<uint32_t>(i);
}
```

### 2.2.3 查找 Present 队列：`vk::PhysicalDevice::getSurfaceSupportKHR`

目标：记录能向当前窗口 surface 展示图像的 family index。

```cpp
if (phyDevice.getSurfaceSupportKHR(i, surface)) {
    queueFamilyIndices.presentQueue = static_cast<uint32_t>(i);
}
```

### 2.2.4 满足条件后提前结束遍历：`QueueFamilyIndices::operator bool`

目标：当 Graphics + Present 都找到后立即结束扫描，降低初始化开销。

```cpp
if (queueFamilyIndices) {
    break;
}
```

## 2.3 示例：一个物理设备上的 Queue Family 布局

下面给一个典型示例（假设枚举结果）：

1. Family 0
   - `queueFlags = Graphics | Compute | Transfer`
   - `queueCount = 1`
   - 支持 Present
2. Family 1
   - `queueFlags = Compute | Transfer`
   - `queueCount = 2`
   - 不支持 Present
3. Family 2
   - `queueFlags = Transfer`
   - `queueCount = 1`
   - 不支持 Present

一种常见使用方式：

1. 渲染命令提交到 Family 0 的 Queue 0（Graphics）；
2. 异步计算提交到 Family 1 的 Queue 0（Compute）；
3. 资源上传提交到 Family 2 的 Queue 0（Transfer）；
4. 最终显示使用支持 Present 的 family（本例是 Family 0）。

这也说明了两点：

1. 一个 PhysicalDevice 往往有多个 family；
2. 每个 family 下可以有多个 queue，且同 family 内 queue 能力相同。

## 2.4 Graphics 与 Compute 能否共用同一个 Queue

可以。  
如果某个 queue 的 `queueFlags` 同时包含 `vk::QueueFlagBits::eGraphics` 和 `vk::QueueFlagBits::eCompute`，那么渲染命令和计算命令都可以提交到这一个 queue。

### 2.4.1 共用同一 Queue 的特点

1. **可行且常见**：很多入门或中小项目都用一个 graphics queue 同时处理图形与计算；
2. **同步更简单**：同一 queue 上按提交顺序执行，通常不需要跨 queue semaphore；
3. **并行性有限**：同一 queue 的工作是有序推进，难以获得真正的图形/计算重叠执行。

### 2.4.2 什么时候拆分到不同 Queue

当你希望提高并发潜力（例如图形渲染与异步计算重叠）时，可以改为不同 queue（同 family 下不同 queue，或专用 compute family）。  
但要注意：拆分后需要跨 queue 同步，且“是否真的并行”仍取决于硬件与驱动实现。

```cpp
// 共用同一 queue（顺序执行）
graphicsQueue.submit(drawSubmitInfo, drawFence);
graphicsQueue.submit(computeSubmitInfo, computeFence);

// 拆分不同 queue（更复杂，需要 semaphore 串联依赖）
graphicsQueue.submit(drawSubmitInfo, drawFence);
computeQueue.submit(computeSubmitInfo, computeFence);
```

# 3 Device

## 3.1 概念

`vk::Device`（C API: `VkDevice`）是从 `PhysicalDevice` 派生出来的“逻辑设备”。  
它代表应用与 GPU 的会话上下文，负责：

1. 创建设备级资源（buffer/image/pipeline 等）；
2. 提供 `vk::Queue` 句柄用于提交命令；
3. 声明并启用设备扩展（如 swapchain）。

可以把关系理解为：

- `PhysicalDevice`：硬件能力清单；
- `Device`：你向这块硬件申请到的“可操作实例”；
- `Queue`：`Device` 中真正执行命令的通道。

## 3.2 流程

### 3.2.1 准备设备扩展：`VK_KHR_SWAPCHAIN_EXTENSION_NAME`

目标：启用窗口呈现所需的 swapchain 扩展。

```cpp
std::array extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
```

### 3.2.2 申请队列：`vk::DeviceQueueCreateInfo`

目标：按“**队列族**”申请 queue。  
`vk::DeviceQueueCreateInfo` 描述的是“从某个 family 申请多少个 queue”，不是“只申请某一种用途”。  
因此这里更准确的规则是：

1. 如果 `graphicsQueue` 和 `presentQueue` 是**同一个 family index**，只需要 1 组 `DeviceQueueCreateInfo`；
2. 只有当它们是**不同 family index** 时，才需要 2 组 `DeviceQueueCreateInfo`。

```cpp
std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
float priority = 1.0f;

vk::DeviceQueueCreateInfo queueCreateInfo;
queueCreateInfo.setPQueuePriorities(&priority)
               .setQueueCount(1)
               .setQueueFamilyIndex(queueFamilyIndices.graphicsQueue.value());
queueCreateInfos.push_back(queueCreateInfo);

if (queueFamilyIndices.graphicsQueue.value() != queueFamilyIndices.presentQueue.value()) {
    vk::DeviceQueueCreateInfo presentQueueCreateInfo;
    presentQueueCreateInfo.setPQueuePriorities(&priority)
                          .setQueueCount(1)
                          .setQueueFamilyIndex(queueFamilyIndices.presentQueue.value());
    queueCreateInfos.push_back(presentQueueCreateInfo);
}
```

`setPQueuePriorities` 的作用是给**本组要创建的 queue**设置调度优先级数组（范围 `0.0f ~ 1.0f`）。  
它与 `queueCount` 一一对应：如果 `queueCount = N`，就要提供至少 `N` 个 `float` 优先级值。

```cpp
// 申请 2 条 queue，就要给 2 个优先级
std::array<float, 2> priorities = {1.0f, 0.5f};
queueCreateInfo.setQueueCount(2)
               .setPQueuePriorities(priorities.data());
```

对当前代码来说，每个 family 只申请 1 条 queue，所以传一个 `float priority` 即可。  
另外，优先级只在**同一设备、同一 family 的多个 queue 争用执行资源**时才有意义，是否严格生效由驱动实现决定。

补充说明：

1. 这里 `queueCount = 1`，表示每个相关 family 先申请 1 条 queue，足够当前项目使用；
2. 当 graphics/present 同 family 时，你后面 `getQueue(familyIndex, 0)` 取到的通常就是同一个 queue 句柄；
3. `pQueuePriorities` 指向的优先级数组在 `createDevice` 调用期间必须保持有效，上面示例用局部变量满足这个要求。

### 3.2.3 创建设备：`vk::PhysicalDevice::createDevice`

目标：把“队列申请 + 扩展启用”写入 `vk::DeviceCreateInfo` 并创建逻辑设备。

```cpp
vk::DeviceCreateInfo createInfo;
createInfo.setQueueCreateInfos(queueCreateInfos)
          .setPEnabledExtensionNames(extensions);

device = phyDevice.createDevice(createInfo);
```

### 3.2.4 获取队列句柄：`vk::Device::getQueue`

目标：拿到后续提交命令要用的 graphics/present queue。

```cpp
graphicsQueue = device.getQueue(queueFamilyIndices.graphicsQueue.value(), 0);
presentQueue = device.getQueue(queueFamilyIndices.presentQueue.value(), 0);
```

## 3.3 生命周期与顺序要求

1. 创建顺序应为：`PhysicalDevice -> QueueFamilyIndices -> Device -> Queue`；
2. 销毁顺序应与依赖相反（先销毁 `device`，再销毁 `instance`）；
3. 若后续加入更多队列用途（compute/transfer），要在 `createDevice` 阶段一并声明对应 family。
