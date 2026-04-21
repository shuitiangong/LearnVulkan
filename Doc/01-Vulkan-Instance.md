# 1 Vulkan Instance

## 1.1 概念

`vk::Instance`（C API: `VkInstance`）是 Vulkan 初始化链路中的全局入口对象。  
它不直接负责渲染，但它决定了“应用能使用哪些实例能力”，例如：

- 是否启用验证层
- 是否启用窗口系统相关扩展（surface）
- 是否启用调试扩展（debug utils）

后续很多步骤都依赖 `Instance`，包括：

- 枚举物理设备（`enumeratePhysicalDevices`）
- 创建窗口 surface（`vk::SurfaceKHR`）
- 创建 Debug Messenger

从工程实践上看，`Instance` 是“能力声明 + 全局上下文”的组合。

## 1.2 初始化流程

### 1.2.1 准备应用信息：`vk::ApplicationInfo`

作用：向驱动声明应用与引擎基础信息，以及目标 Vulkan API 版本。  
注意：`setApplicationVersion` 是应用版本；`setApiVersion` 才是 Vulkan API 版本。

```cpp
vk::ApplicationInfo appInfo{};
appInfo.setPApplicationName("Toy2D")
       .setApplicationVersion(1)
       .setApiVersion(VK_API_VERSION_1_3);
```

---

### 1.2.2 收集并检查实例扩展：`vk::enumerateInstanceExtensionProperties`

作用：查询系统当前支持的实例扩展，避免“创建时直接失败”。  
窗口扩展通常来自 SDL/GLFW，调试场景常追加 `VK_EXT_debug_utils`。

```cpp
auto exts = vk::enumerateInstanceExtensionProperties();
bool hasDebugUtils = std::any_of(exts.begin(), exts.end(),
    [](const vk::ExtensionProperties& p) {
        return std::strcmp(p.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
    });
```

---

### 1.2.3 组装创建参数：`vk::InstanceCreateInfo`

作用：把应用信息、层、扩展统一写入创建参数。

```cpp
vk::InstanceCreateInfo ci{};
ci.setPApplicationInfo(&appInfo)
  .setPEnabledLayerNames(enabledLayers)
  .setPEnabledExtensionNames(enabledExtensions);
```

---

### 1.2.4 创建实例：`vk::createInstance`

```cpp
vk::Instance instance = vk::createInstance(ci);
```

创建成功后才能继续创建 surface、debug messenger、physical device 等对象。

## 1.3 生命周期与常见问题

1. `Instance` 通常在初始化早期创建，在退出阶段销毁。  
2. 如果启用了 debug messenger，销毁顺序应是：先 debug messenger，再 instance。  
3. 忘记启用窗口扩展时，surface 创建会失败。  
4. 把 `setApplicationVersion` 当作 API 版本是常见误用。

---

# 2 验证层（Validation Layer）

## 2.1 概念

验证层是 Vulkan 的运行时规范检查机制。  
它不会修复错误，但会尽可能早地报告“哪里违反了 Vulkan 规则”。

最常用层：

- `VK_LAYER_KHRONOS_validation`

它最有价值的场景：

- 同步问题（semaphore/fence 使用错误）
- 资源生命周期问题（先销毁后使用）
- 图像布局问题（present 前布局不正确）
- 参数问题（缺失 stage mask、无效句柄等）

## 2.2 启用流程

### 2.2.1 查询可用层：`vk::enumerateInstanceLayerProperties`

```cpp
auto layers = vk::enumerateInstanceLayerProperties();
bool hasValidation = std::any_of(layers.begin(), layers.end(),
    [](const vk::LayerProperties& p) {
        return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    });
```

---

### 2.2.2 写入 Instance 创建参数：`setPEnabledLayerNames`

```cpp
std::vector<const char*> enabledLayers;
if (hasValidation) {
    enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
}
ci.setPEnabledLayerNames(enabledLayers);
```

## 2.3 实践建议

1. 开发阶段默认开启验证层。  
2. 发布版本可关闭验证层（减少运行开销与日志噪声）。  
3. 报错信息不要只看“现象”，应按 VUID 和调用栈定位根因。

---

# 3 DebugMessenger

## 3.1 概念

Debug Messenger 是验证层消息的接收器。  
验证层负责发现问题，Debug Messenger 负责把问题回调到你的日志系统。

它依赖实例扩展：

- `VK_EXT_debug_utils`

消息级别一般包括：

- Verbose / Info / Warning / Error

## 3.2 创建流程

### 3.2.1 定义回调函数

```cpp
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    std::cerr << "[Vulkan Validation] " << data->pMessage << '\n';
    return VK_FALSE;
}
```

---

### 3.2.2 填写创建信息：`VkDebugUtilsMessengerCreateInfoEXT`

```cpp
VkDebugUtilsMessengerCreateInfoEXT ci{};
ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
ci.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
ci.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
ci.pfnUserCallback = DebugCallback;
```

---

### 3.2.3 通过 `vkGetInstanceProcAddr` 获取扩展函数并创建

```cpp
auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkCreateDebugUtilsMessengerEXT"));

VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
createFn(static_cast<VkInstance>(instance), &ci, nullptr, &messenger);
```

---

### 3.2.4 销毁 Debug Messenger

```cpp
auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkDestroyDebugUtilsMessengerEXT"));
destroyFn(static_cast<VkInstance>(instance), messenger, nullptr);
```

## 3.3 与验证层的关系

- 验证层：负责检查 Vulkan 使用是否合法。  
- Debug Messenger：负责接收并输出这些检查结果。  

二者通常搭配使用：  
**启用验证层 + 注册 Debug Messenger**，才能在开发阶段高效定位问题。
