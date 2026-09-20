---
name: f1c100s-ops
description: Allwinner F1C100s automated build, size audit, and dual-stage flashing toolchain for OpenVela. Use when building, testing, auditing firmware size budgets, or flashing F1C100s boards via Fastboot or xfel.
---

# Allwinner F1C100s Operations & Flashing Skill

本 Skill 为全志 F1C100s 硬件平台提供 AI 自动化构建、固件尺寸合规审查与免拆无感烧录操作指南。

## 适用场景
- 编译 5 份量产级配置中的任一份（`bl`, `nsh`, `nsh-cdc`, `nsh-sdio`, `nsh-net`）
- 审查各固件是否满足 3MB AP Flash 分区预算
- 触发免按键 Fastboot 无感增量热更
- 首次烧录或救砖时的 xfel SPI NOR 全量擦写
- 免刷 Flash 的 DRAM 极速加载测试

---

## 1. 固件编译操作

构建入口在 OpenVela 工作区根目录（专属仓上一级）：

```bash
# 进入工作区根目录
cd /home/elliot/mywork/openvela-contest-workspace

# 编译指定配置（支持 -j 并行）
./build.sh -m vendor/allwinner/boards/f1c100s/demo/configs/<config_name>/ -j$(nproc)
```

支持的 `<config_name>`：
- `bl`: Bootloader 引导固件 (Fastboot 监听)
- `nsh`: 完整功能控制台 (ADB + ST7789 屏显)
- `nsh-cdc`: CDC ACM 虚拟串口控制台
- `nsh-sdio`: SD/TF 卡大容量文件系统支持
- `nsh-net`: USB RNDIS 虚拟网卡 + Web 协议栈

---

## 2. 固件大小与预算硬核核验

在修改代码或重新编译后，必须运行大小合规核验工具：

```bash
python3 vendor/allwinner/tools/check_firmware_size.py
```

### 预算规则与红线
- AP 分区总预算：**3 MiB (3,145,728 Bytes)**
- 告警阈值：若单固件占用超过预算的 80%，需分析符号表做深度裁剪
- 实测基线：各配置当前占用在 5.51% ~ 26.73% 之间，保持充裕余量

---

## 3. 自动化烧录与调试

### 模式 A：免按键 Fastboot 无感热更新（推荐日常调试）
在设备开机运行态（AP 阶段），由宿主机直接触发：

```bash
bash vendor/allwinner/tools/flash-ap-fastboot.sh path/to/nuttx.bin
```
**工作流程**：
1. 宿主机通过 ADB / 串口向板端发送 `reboot bootloader`
2. 板端看门狗软复位，进入 BL Fastboot 模式
3. 宿主机检测到 fastboot 设备，擦写 0x0B1000 AP 分区
4. 自动发送 `fastboot reboot` 启动新固件（耗时约 4 秒）

### 模式 B：首次烧录或救砖（xfel 模式）
按住板载 FEL 键上电插入 USB：

```bash
# 全量擦写 SPI NOR 并烧录 SPL + Bootloader + AP 固件
bash vendor/allwinner/tools/flash-spinor.sh path/to/nuttx.bin
```

### 模式 C：免擦写 Flash DRAM 直接运行测试
用于快速验证临时构建的内核或诊断固件：

```bash
bash vendor/allwinner/tools/boot-dram.sh path/to/nuttx.bin
```
