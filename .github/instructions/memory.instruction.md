---
applyTo: '**'
---

# 会话续接：CNC TS/RA2 Mission Editor（FinalSun）重构

用户任务（中文）：**避免上帝类；扫描整个项目；内存危险的代码全部放进 Rust。**

当前分支 main，工作区有未提交改动（不要主动 commit，除非用户要求）。

## 已完成（均已构建验证 + 测试通过）

### 1. Rust 迁移 —— 内存危险代码（核心成果）
- `rust_core/src/codec.rs`：base64 / Format80 / Format5(LZO) / IsoMapPack5 编解码全部移植进 Rust，逐字节边界检查。
- `rust_core/src/csf.rs`：RA2/YR CSF 字符串表解析。
- `rust_core/src/minilzo.rs` + `rust_core/vendor/minilzo/`：minilzo 2.10 重命名符号 vendor（minilzo_rs.c 用 #define 前缀 rs_core_），避开与 lzo2.lib 的 LNK2005 冲突。ABI 注意：`lzo_uint == size_t`，长度用 usize。GPL v2+ 与项目 GPLv3 兼容。
- `rust_core/build.rs`：cc crate 编译 vendor 的 C。
- C++ 接入：FSunPackLib（含 `DecodeIsoMapPack5` 新增 `size_t dp_cap` 参数）、`CLoading::LoadStrings`、`MapData::Unpack` 全部改走 Rust；RustCore.h 声明齐全。
- 测试：`cargo test --target x86_64-pc-windows-msvc` 22 项通过；C++ tests.cpp 9 组（含 test_codecs/test_csf）全过。

### 2. 上帝类拆分
- `MissionEditor/MapSnapshots.h/.cpp`：`CMapSnapshots`（快照环/Undo/Redo，before/after 回调处理钱与 minimap）。
- `MissionEditor/FieldData.h`：NODEDATA/MAPFIELDDATA/FIELDDATA + OVERLAY_PACK_* 常量。
- `MissionEditor/MapMinimap.h/.cpp`：`CMapMinimap`（minimap DIB、GetCellPixelPos、WriteCell）；MapData.h 里原来的 ~200 行内联 Mini_UpdatePos/GetMiniMapPos 已移出到 MapData.cpp。
- MapData.h/.cpp 相应瘦身；vcxproj 已注册全部新文件。

### 3. 固定缓冲区溢出修复（本轮）
- `currentMapFile`：char[MAX_PATH+1] → **CString**（variables.h/.cpp + FinalSun/FinalSunDlg 全部 strcpy 改赋值/Empty/IsEmpty）。
- `TSPath`：唯一写入点改 `strcpy_s`（截断而非溢出）。
- `CFloatEdit::OnKillfocus`：_fcvt 静态缓冲区 strcat 改为 CString 左补零。
- `CInputBox::OnOK`：**off-by-one 堆溢出**（new char[GetLength()] 少 NUL）→ +1。
- `TranslateStringVariables`：固定缓冲 → CString::Format。
- `PosToXY`：strcpy 固定缓冲 + Posleng<3 读越界 → CString Left/Right。
- `GetNodeName`：itoa char[5] 溢出（n≥1000）→ CString（并在 functions.h 加了声明）。
- `GetNodeAt` 的 Foundation 解析：memcpy 定长 → CString Left/Mid。
- Tooltip `pTTT->szText`：截断 80→79（**off-by-one**）。
- `TriggerActionsDlg`：wayp char[50]→CString；strchr 循环加 NULL 保护（防崩溃）。
- `UserScriptsDlg`：`memset(jumplinename,512,0)` 参数反了是 no-op → 修正为 0/sizeof；strcat 加长度护栏。

## 验证命令
- 构建（必须走 sln，prebuild 用 $(SolutionDir) 找 rust_core）：
  `& "D:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe" MissionEditor.sln /p:Configuration="FinalAlertDebug YR" /p:Platform=x64 /m`
- 测试：`/p:Configuration="Tests FinalAlertDebug YR"`，产物 `dist\FinalAlert2YR\FinalAlert2YRTestsd.exe`（直接跑，看 Failed/Succeeded，当前 0/9）。
- Rust：`cargo test` + `cargo test --target x86_64-pc-windows-msvc`（rust_core 目录）。

## 环境陷阱
- clangd/LSP 报错全是噪音（没有 MFC 包含路径），以 MSBuild 为准。
- LNK4098（MSVCRT 冲突）是既有 warning，无害。
- 编辑 CRLF 文件时用精确文本替换；空白行数必须完全一致。
- xcc encode5/decode5 分节 8192 字节；EncodeF80 分节头 [size_in u16][packLen byte2][0x20]。
- minilzo 的长度 ABI 必须与 C 的 `size_t` 对齐；自写 minilzo.rs 使用 `usize`，勿改回固定宽度整数。

## 可继续（未做，可选）
- CFinalSunDlg 文件操作（OpenMap/SaveMap/currentMapFile 相关）抽 CMapFileIO。
- CIsoView（202KB）拆分；CLoading 进一步瘦身。
- 其余 strcat（MapData 地形/路径点 char[15]、Loading.cpp 的 itoa+FS）输入有界、风险低，未动。
- IsoView.cpp DDS 行拷贝 memcpy（5842/5878）与 LineDrawer memcpy 可考虑 Rust 化。
- 收尾后若用户要求再 commit。
