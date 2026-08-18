# FinalSun / FinalAlert 2 项目遗留代码性能分析与改造建议报告

## 目录
- [一、概述](#一概述)
- [二、配置系统与字符串检索优化（CIniFile）](#二配置系统与字符串检索优化cinifile)
- [三、VXL 体素渲染与数学计算管线优化（rust_core / PackLib）](#三vxl-体素渲染与数学计算管线优化rust_core--packlib)
- [四、核心地图数据与对象生命周期优化（FIELDDATA / CMapData）](#四核心地图数据与对象生命周期优化fielddata--cmapdata)
- [五、渲染管线与内存访问局部性优化（IsoView / Blit / MiniMap）](#五渲染管线与内存访问局部性优化isoview--blit--minimap)
- [六、高频业务计算与交互事件节流优化](#六高频业务计算与交互事件节流优化)
- [七、废弃与低效 Win32 API 替换](#七废弃与低效-win32-api-替换)
- [八、优化优先级与实施路径建议](#八优化优先级与实施路径建议)

---

## 一、概述

本项目基于早期 FinalSun / FinalAlert 2 经典 MFC 架构演进，经过近期的 Rust 核心库与 Vulkan 渲染接入，性能与安全性已得到显著改善。然而， codebase 中仍保留了大量早期的 C++ 编码范式与未优化的算法逻辑。

主要性能瓶颈集中在：
1. **算法复杂度退化**：在 `std::map` 结构上使用下标索引模拟随机访问，导致大量 $O(N^2)$ 至 $O(N^3)$ 操作。
2. **非 POD 对象按值传递**：大型二维地图数组中每个格子内嵌非平坦对象（包含 `CString`），在绘制与快照遍历中触发海量引用计数与构造/析构开销。
3. **数学管线冗余计算**：VXL 体素渲染逐点执行极坐标三角函数（`sqrt` / `atan2` / `sin` / `cos`）与多次矩阵乘法。
4. **内存访问局部性差（Cache Thrashing）**：大量双重循环采用列优先（X 外层、Y 内层）跨步访问，导致 CPU L1/L2 缓存与预取器失效。
5. **逐像素 API / 函数调用开销**：逐像素执行 `memcpy`、`SetPixel` 或 Win32 废弃接口 `IsBadWritePtr`。

---

## 二、配置系统与字符串检索优化（CIniFile）

### 1. 消除 `std::map` 下标遍历导致的 $O(N^2)$ 退化
- **涉及文件**：
  - `MissionEditor/IniFile.h` (第 78~99 行)
  - `MissionEditor/IniFile.cpp` (第 301~324 行、第 341~364 行、第 458~504 行)
  - `MissionEditor/Loading.cpp` (第 188~251 行、第 6483~6492 行)
  - `MissionEditor/functions.cpp` (第 1435~1460 行)
- **问题分析**：
  - `CIniFile` 和 `CIniFileSection` 使用 `std::map<CString, ...>` 存储。
  - 为了兼容旧代码，提供了 `GetSection(index)`、`GetValue(index)`、`GetValueName(index)`，其实现是每次从 `begin()` 开始逐个迭代步进 `index` 步（单次 $O(index)$）。
  - 在 `Loading.cpp`（如合并 `firestrm.ini` 与 `rules.ini`）以及规则读取等处，大量使用形如 `for (i = 0; i < sections.size(); i++)` 配合 `GetSection(i)` 的写法，使遍历整体退化为 **$O(N^2)$**。
  - 在 `functions.cpp` 中还存在嵌套调用 `FindValue` + `GetValueName` + `FindValue`，导致局部复杂度甚至达到 **$O(N^3)$**。
- **改造方案**：
  1. **双重索引容器**：将 `CIniFileSection` 内部存储改造为 `std::vector<std::pair<CString, CString>>`（保留文件原始顺序，下标随机访问 $O(1)$）配合 `std::unordered_map<CString, size_t>`（字符串按键查找 $O(1)$）。
  2. **全面替换旧遍历**：废弃并移除 `GetSection(index)` 等接口，全面改用 C++20 基于范围的 for 循环：
     ```cpp
     for (const auto& [key, value] : section) { ... }
     ```
  3. **文本解析优化**：在 `InsertFile` 解析行文本时，减少 `std::string::substr` 与临时 `CString` 的频繁构造，改用 `std::string_view` 进行分词裁剪。

---

## 三、VXL 体素渲染与数学计算管线优化（rust_core / PackLib）

### 1. 消除体素旋转中的三角函数与开方计算
- **涉及文件**：
  - `rust_core/src/lib.rs` (第 205~234 行 `rotate_*` 函数)
  - `MissionEditorPackLib/MissionEditorPackLib.cpp` (第 1365~1397 行)
- **问题分析**：
  - 目前 `rotate_x` / `rotate_y` / `rotate_z` 采用极坐标转换公式：
    ```rust
    fn rotate_x(v: &mut Vec3, a: f32) {
        let l = (v.y * v.y + v.z * v.z).sqrt();
        let d_a = v.y.atan2(v.z) + a;
        v.y = l * d_a.sin();
        v.z = l * d_a.cos();
    }
    ```
  - 数学上：
    $$\begin{aligned}
    v_y' &= v_y \cos(a) + v_z \sin(a) \\
    v_z' &= -v_y \sin(a) + v_z \cos(a)
    \end{aligned}$$
  - 当前代码在每个体素点上重复执行了 3 次 `sqrt`、3 次 `atan2`、3 次 `sin`、3 次 `cos`。对于包含数万体素的复杂模型（如天启坦克、基洛夫等），每帧产生数十万次昂贵的浮点超越函数调用。
- **改造方案**：
  - **3x3 旋转矩阵化**：在模型或 Section 级别提前计算好欧拉角旋转矩阵 $R_{zxy}$（仅需 1 次 $\sin/\cos$ 计算）。
  - 对每个顶点仅做标准的向量矩阵乘法（9 次乘法 + 6 次加法），完全消除 `sqrt` 和 `atan2`。

### 2. 仿射变换预乘合并与 Z 轴差分步进
- **涉及文件**：
  - `rust_core/src/lib.rs` (第 522~545 行 `rs_vxl_render_section`)
- **问题分析**：
  - 在最内层体素跨度遍历中，每个体素均串行执行：
    ```rust
    let m_pixel = translate_to_world_matrix.mul_vec(scale_to_world_matrix.mul_vec(s_pixel));
    let t_pixel = scaled_matrix.mul_vec(m_pixel);
    let mut d_pixel = t_pixel;
    rotate_zxy(&mut d_pixel, &rotation);
    d_pixel = d_pixel + model_offset;
    ```
  - 所有变换均为固定线性变换，且 $x, y$ 在外层固定，$z$ 以步长 1 连续递增。
- **改造方案**：
  1. **变换矩阵合并**：在循环外预计算复合矩阵 $M = M_{\text{model}} \cdot R \cdot M_{\text{scaled}} \cdot M_{\text{trans}} \cdot M_{\text{scale}}$。
  2. **Z 轴差分计算（DDA）**：由于 $z \to z+1$，直接使用 $P(x, y, z+1) = P(x, y, z) + \frac{\partial M}{\partial z}$，将乘法完全降为单次加法。
  3. **法线光照预计算查找表（LUT）**：体素法线索引仅为 `u8`（0~255），可在进入体素循环前预先计算 256 长度的光照强度表 `let lut: [u8; 256]`，内层循环直接 $O(1)$ 查表。

---

## 四、核心地图数据与对象生命周期优化（FIELDDATA / CMapData）

### 1. `FIELDDATA` 结构体 POD 化与传参优化
- **涉及文件**：
  - `MissionEditor/FieldData.h` (第 31~37 行、第 60~93 行)
  - `MissionEditor/IsoView.cpp` (第 7052 行、第 7080 行等)
  - `MissionEditor/MapData.cpp` (多处)
- **问题分析**：
  - `FIELDDATA` 结构体内嵌 `NODEDATA node;`，而 `NODEDATA` 包含 `CString house;`。
  - 这导致 `FIELDDATA` 成为非 TriviallyCopyable 类型。每次值拷贝（包括构造和析构）都会伴随 MFC `CString` 的引用计数增减或堆管理。
  - 在 `CIsoView::DrawMap()` 绘制循环中，每帧对屏幕内数千/数万个格子都在执行 `FIELDDATA m = *Map->GetFielddataAt(...);`（按值拷贝）。
- **改造方案**：
  1. **移除 CString 字段**：将 `NODEDATA::house` 改为所属方索引 `short houseId` 或轻量句柄，使 `FIELDDATA` 成为平坦的纯 POD 内存结构。
  2. **启用直接内存拷贝**：使 `std::vector<FIELDDATA>` 的重分配、快照以及多线程操作支持直接 `memcpy`。
  3. **传递常量引用**：在 `DrawMap` 等只读场景中，将值拷贝改为 `const FIELDDATA& m = *Map->GetFielddataAt(...);`。

---

## 五、渲染管线与内存访问局部性优化（IsoView / Blit / MiniMap）

### 1. 纠正双重循环的列优先访问模式（Cache Thrashing）
- **涉及文件**：
  - `MissionEditor/MapSnapshots.cpp` (第 94~111 行 `TakeSnapshot`, 第 125~149 行 `RestoreSnapshot`)
  - `MissionEditor/MapData.cpp` (第 7393~7400 行 `RedrawMinimap`)
  - `MissionEditorPackLib/MissionEditorPackLib.cpp` (第 1185~1204 行 `LoadTMPImageInSurface`)
- **问题分析**：
  - 数组和 DIB 图像在内存中是按行连续存储（Row-Major）的。
  - 上述代码大量使用了外层 X（`i` 从 0 到 `width`）、内层 Y（`e` 从 0 到 `height`）的双重循环结构。
  - 内层循环每次递增 `e` 都会跨越整整一行（Pitch 跨度数百至数千字节），造成几乎 100% 的 L1/L2 Cache Miss，导致 CPU 硬件预取器完全失效。
- **改造方案**：
  - 统一重构所有图像与地图遍历的嵌套循环，改为**外层 Y（行）、内层 X（列）**：
    ```cpp
    for (int y = 0; y < height; ++y) {
        BYTE* destRow = dest + y * pitch;
        const BYTE* srcRow = src + y * width;
        for (int x = 0; x < width; ++x) {
            // 连续内存线性顺序访问
        }
    }
    ```

### 2. `BlitTerrain` / `BlitPic` 像素内层循环展开与向量化
- **涉及文件**：
  - `MissionEditor/IsoView.cpp` (第 359~483 行 `BlitTerrain`, 第 680~793 行 `BlitPic`)
- **问题分析**：
  - 在光栅化内层循环中，逐像素计算目标地址：`((BYTE*)dst + (blrect.left + i) * bpp + (blrect.top + e) * dpitch)`。
  - 对每个单独像素调用 `memcpy(dest, &iPalIso[val], bpp)`。
  - `BlitPic` 在内层循环逐像素判断 `if (pLighting)` 并逐通道除法运算。
- **改造方案**：
  - 计算每行的起始指针 `rowDst` 与 `rowSrc`，内层仅做指针单步自增。
  - 针对 16bpp (`uint16_t*`) 和 32bpp (`uint32_t*`) 采用强类型直接赋值或 SIMD 指令批量填充。
  - 将 `pLighting` 分支移至外层循环，提供带光照与不带光照两条特化内联路径。

---

## 六、高频业务计算与交互事件节流优化

### 1. 循环内重复的 INI 查询与字符串计算外提
- **涉及文件**：
  - `MissionEditor/MapData.cpp` (第 6356~6387 行 `CalcMoneyOnMap`)
  - `MissionEditor/functions.cpp` (第 146~169 行 `TranslateHouse`)
- **问题分析**：
  - `CalcMoneyOnMap` 遍历地图几十万个格子时，在最内层重复调用 `atoi(rules.sections["Riparius"].values["Value"])` 等 4 个红黑树查找和字符串解析。
  - `TranslateHouse` 每次调用都在循环中执行 `CString.Replace` 并线性查找规则。
- **改造方案**：
  - 在 `CalcMoneyOnMap` 循环前将矿物单价解析为局部整型常量（`const int valRiparius = ...`）。
  - 建立 `TranslateHouse` 的静态哈希查找表（`std::unordered_map<CString, CString>`），将多次字符串替换降为单次哈希查找。

### 2. 鼠标笔刷预览节流与脏区域局部重绘
- **涉及文件**：
  - `MissionEditor/IsoView.cpp` (第 1220~1340 行 `OnMouseMove`)
- **问题分析**：
  - 鼠标移动预览地表或物件时，每个 `WM_MOUSEMOVE` 事件都完整触发一次 `TakeSnapshot -> SetTile -> CreateShore -> SmoothAll -> DrawMap -> Undo`。
  - 在高回报率电竞鼠标（500Hz~1000Hz）下会引发事件洪泛，导致 CPU 占用率 100% 及明显掉帧。
- **改造方案**：
  - 引入帧率/时间节流（如 16ms 限制），合并多余的鼠标移动消息。
  - 将自动修边和重绘范围严格限定在笔刷覆盖的局部包围盒（Bounding Box）脏矩形内，避免触发整屏完整重绘。

---

## 七、废弃与低效 Win32 API 替换

### 1. 移除 `IsBadWritePtr`
- **涉及文件**：
  - `MissionEditorPackLib/MissionEditorPackLib.cpp` (第 1180 行)
- **问题分析**：
  - `IsBadWritePtr` 是 Windows 早期遗留 API，其原理是通过安装 SEH 异常处理器并尝试写入页面以探测有效性。不仅存在安全隐患，且执行开销极大，已被微软官方废弃。
- **改造方案**：
  - 彻底移除 `IsBadWritePtr` 调用，通过上游明确的缓冲区边界校验与 `ASSERT` 确保内存安全。

### 2. 替换 GDI `SetPixel` 逐点绘制
- **涉及文件**：
  - `MissionEditorPackLib/MissionEditorPackLib.cpp` (第 1257 行)
- **问题分析**：
  - 在图块 GDI 兼容回退路径中，逐像素调用 Win32 `SetPixel(hDC, ...)`。单个 128x64 图块会产生 8,192 次用户态到内核态的 GDI 切换。
- **改造方案**：
  - 改用内存 DIB 缓冲区直接写入，最后单次调用 `SetDIBitsToDevice` / `BitBlt` 批量提交。

---

## 八、优化优先级与实施路径建议

| 优先级 | 优化项 | 预期收益 | 影响范围 |
| :--- | :--- | :--- | :--- |
| **P1（极高）** | **VXL 体素渲染数学管线重构**<br>（消除极坐标三角/开方计算 + 矩阵预乘 + 法线 LUT） | 单位/建筑 VXL 渲染提速 **10x ~ 50x**，显著提升单位密集场景帧率 | `rust_core/src/lib.rs`<br>`MissionEditorPackLib.cpp` |
| **P1（极高）** | **CIniFile 容器与遍历重构**<br>（消除 $O(N^2)$ 下标遍历，改用双重索引容器与 C++20 迭代器） | 大型 MOD（如 Mental Omega / MO3）地图加载与 rules 解析提速 **5x ~ 20x** | `IniFile.cpp`<br>`Loading.cpp` |
| **P2（高）** | **FIELDDATA POD 化与常量引用优化**<br>（移除内嵌 CString，绘制循环消除按值复制） | 消除每帧数十万次原子引用计数增减与堆操作，降低内存峰值 | `FieldData.h`<br>`IsoView.cpp` |
| **P2（高）** | **内存循环访问由列优先改为行优先**<br>（Snapshots / MiniMap / TMP Surface 遍历重构） | 消除 Cache Miss，小地图重绘与 Undo/Redo 历史快照提速 **3x ~ 8x** | `MapSnapshots.cpp`<br>`MapData.cpp` |
| **P2（高）** | **Blit 光栅化内层循环优化**<br>（指针增量步进 + 强类型直接写入 + 光照分支外提） | 地图平移与主视口渲染 CPU 占用率显著降低 | `IsoView.cpp` |
| **P3（中）** | **高频计算外提与查表缓存**<br>（CalcMoneyOnMap 提常量 + TranslateHouse 哈希化） | 减少冗余计算，提升 UI 刷新与状态更新响应度 | `MapData.cpp`<br>`functions.cpp` |
| **P3（中）** | **鼠标笔刷放置预览节流与局部更新** | 消除高回报率鼠标下的拖动卡顿与 CPU 尖峰 | `IsoView.cpp` |
| **P3（中）** | **废弃与低效 Win32 API 移除**<br>（移除 IsBadWritePtr + SetPixel 批处理） | 提升图块载入性能，符合现代 Windows 编程安全标准 | `MissionEditorPackLib.cpp` |
