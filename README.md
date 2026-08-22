# VoxMine — 原生多线程的类 Minecraft 体素游戏（Vulkan）

一个用 C++20 编写、使用手写 Vulkan 渲染器与原生工作线程的沙盒类 Minecraft 游戏。
内容参考早期 Minecraft（Beta ~1.8 时代）：草方块、泥土、石头、基岩、经典矿石
（煤/铁/金/红石/钻石）、橡树、沙子、砾石、雪地生物群系与海洋。

## 编译

依赖：Vulkan SDK（任意较新的 1.x）、CMake >= 3.20、Ninja、C++20 编译器
（MinGW GCC 12+ 或 MSVC）。

```sh
cmake -G Ninja -S . -B build
cmake --build build
```

构建会复制 `assets/`（方块纹理）与编译好的着色器到可执行文件旁边。

> 说明：`assets/` 中的纹理是从 Minecraft 官方 jar 中提取的，按 Mojang EULA
> 不允许随仓库分发，因此该目录已被加入 `.gitignore`。请使用仓库中的
> `tools/extract_assets` 工具从你自己的 Minecraft 安装中提取。

## 运行

```sh
build/voxmine.exe
```

## 操作

| 按键 | 说明 |
|------|------|
| W / A / S / D | 移动 |
| 鼠标 | 视角 |
| 空格 | 跳跃（飞行时上升） |
| 空格（双击） | 切换飞行（创造模式，同原版） |
| Shift | 飞行时下降 |
| E | 打开/关闭物品栏（点击选择方块） |
| 左键 | 挖掘方块（水不可挖；基岩可挖） |
| 右键 | 放置方块 |
| 1..9 / 滚轮 | 选择快捷栏格子 |
| T | 时间快进（昼夜加速） |
| Esc | 退出 / 关闭物品栏 |

## 命令行参数

```
--seed N          世界种子（默认 1337）
--render-dist N   区块渲染距离（默认 8）
--threads N       工作线程数（默认 = CPU 核数）
--pos x,y,z       出生位置
--yaw F --pitch F 相机朝向（弧度）
--time F          起始时间，0..1（0.25=早晨，0.5=夜晚）
--screenshot out.png   渲染数帧后保存截图并退出
--frames N        运行 N 帧后退出
--no-ui           隐藏准星/快捷栏/方块高亮
--no-vsync        关闭垂直同步（解锁帧率）
--inventory       （调试）启动时打开物品栏
--drive           （调试）自动向前行走
--break x,y,z     （调试）渲染前挖掉一个方块
```

## 架构

- `src/world.*` 地形生成、区块存储、工作线程池（`World`）、网格化。
  地形由原生 `std::thread` 线程池生成与网格化：按距离优先调度生成，区块及其
  邻居就绪后进行网格化。方块编辑由共享互斥锁保护；网格化器在读锁下复制
  5 区块的边框数据。
- `src/renderer.cpp` Vulkan 管线（不透明地形、半透明水、天空渐变、方块高亮、
  屏幕空间 UI）、带 mipmap 的方块纹理图集、每区块顶点/索引缓冲
  （host-visible，map+memcpy 上传）。
- `src/vk.cpp` 通过 volk 动态加载进行设备/交换链/渲染通道设置（无需静态链接
  vulkan lib）。
- `shaders/` GLSL，构建时由 glslc 编译。

现代 Minecraft 的部分纹理是可染色（灰度）纹理（草顶、树叶、水），这些在
图集构建时被着色。

## 资源提取工具

`tools/extract_assets.cpp`：一个小程序，让用户选择一个 Minecraft 官方 `.jar`
文件，自动提取游戏所需的所有方块纹理到 `assets/block/`。用以下方式编译运行：

```sh
g++ tools/extract_assets.cpp -o extract_assets.exe -lgdi32 -lcomdlg32
extract_assets.exe
```
