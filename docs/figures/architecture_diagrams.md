# OpenVela 全志 F1C100s 架构设计与关键流程图集

本图集为 **2026 首届 openvela AI 硬件开发者大赛**（队伍编号：`499`，队伍名：`buzhaojideINTP`）参赛作品的核心架构图与流程图，用于技术报告与专属仓文档。

---

## 外部独立开源生态（声明与链接）

本参赛工程聚焦于 OpenVela 在 Allwinner F1C100s SoC 上的系统级适配。以下两个组件作为独立的外部开源项目进行解耦联动，**不存放在本比赛仓内**：

- 🔗 **awboot（第一阶段轻量 SPL 引导程序）**：
  GitHub 地址：[https://github.com/ada20204/awboot](https://github.com/ada20204/awboot)
  功能：提供 24KB 极致自洽的一级启动器，完成 CCU 锁相环（408MHz）与 32MB 片内 SIP DDR 内存硬起，解耦商业闭源 boot。
- 🔗 **v86-toy（Web 虚拟验证与远程终端平台）**：
  GitHub 地址：[https://github.com/ada20204/v86-toy](https://github.com/ada20204/v86-toy)
  功能：基于 WebAssembly (v86) 构建的跨平台验证前端，集成 Tailscale 虚拟局域网穿透与音频模拟流，实现浏览器免安装接管远程物理开发板与虚拟仿真。

---

## 图 1：三级引导启动全流程图（BROM → awboot → BL → AP）

展示芯片上电后，从固化 ROM 到最终进入 OpenVela 应用程序的四阶段状态转移与物理存储映射：

```mermaid
flowchart TD
    subgraph S0["阶段 0：芯片原厂固化 BROM (Offset: 0x0)"]
        A["SoC 上电 / 复位"] --> B{"按住 FEL 键 / Flash 无效?"}
        B -- "是" --> C["USB FEL 救砖模式<br/>(Host 端通过 xfel 免驱动读写)"]
        B -- "否 (正常冷启)" --> D["读取 SPI-NOR 首地址 0x000000"]
    end

    subgraph S1["阶段 1：一级引导 awboot / SPL (24 KiB)"]
        D --> E["awboot (SPL)<br/>🔗 github.com/ada20204/awboot"]
        E --> F["初始化 CCU 时钟树 (CPU @ 408MHz)"]
        F --> G["初始化 DRAM 控制器 (32MB SIP DDR)"]
        G --> H["初始化 SPI 控制器并读取 0x010000"]
        H --> I["加载 BL 镜像到 DRAM 0x80000000 并跳转"]
    end

    subgraph S2["阶段 2：二级引导 BL / Fastboot (169 KiB)"]
        I --> J["Bootloader (基于 NuttX 精简模式)"]
        J --> K["读取 0x0B0000 BootKV (A/B槽位 & reboot标记)"]
        K --> L{"检测到热刷指令 / CRC校验失败?"}
        L -- "是 (升级/救砖)" --> M["启动 USB Fastboot 守护<br/>(监听 Host 端 fastboot flash 命令)"]
        M --> N["静默擦写 0x0B1000 AP 分区，写入新固件"]
        N --> J
        L -- "否 (正常冷启)" --> O["等待 100ms 握手窗口无拦截"]
        O --> P["校验 AP 镜像有效性，配置向量表并跳转"]
    end

    subgraph S3["阶段 3：AP 运行层 OpenVela / NuttX (3 MiB 预算)"]
        P --> Q["OpenVela AP 内核启动 (0x80000000)"]
        Q --> R["初始化 MMU 页表、中断控制器、DMA 引擎"]
        R --> S["挂载 VFS 根文件系统与驱动堆栈"]
        S --> T["进入板级业务 APP 与 NSH 交互环境"]
    end

    style S1 fill:#f9f0ff,stroke:#8a2be2,stroke-width:2px
    style S2 fill:#e6f3ff,stroke:#2b7cff,stroke-width:2px
    style S3 fill:#e6ffe6,stroke:#2eb82e,stroke-width:2px
```

---

## 图 2：系统总体架构与亮点 APP 拓扑图

全面展示本作品在**板端应用（APP）**、**外部联动生态（v86-toy / awboot）**与 **AI Agent 闭环开发**之间的协作全貌：

```mermaid
graph TB
    subgraph Host["宿主机 / AI Agent 研发工作站 (Host Machine)"]
        Agent["🤖 AI Agent (Claude Code / Antigravity)<br/>• 驱动重构 • 编译验证 • 根因分析 • 自动化调度"]
        Tools["🛠️ 自动化工程工具链 (vendor/allwinner/tools/)<br/>• flash-ap-fastboot.sh (免按键无感热更)<br/>• check_firmware_size.py (3MB 空间硬门禁)<br/>• boot-dram.sh (免刷 Flash 极速测试)"]
        Agent --> Tools
    end

    subgraph ExtRepos["外部独立联动生态 (External Repositories)"]
        AWBOOT["🔗 独立仓库：ada20204/awboot<br/>Allwinner F1C100s/T113 专属轻量 SPL<br/>负责 DRAM 内存硬起与时钟初始化"]
        V86["🔗 独立仓库：ada20204/v86-toy<br/>基于 WebAssembly 的虚拟验证与 Web 终端<br/>集成 Tailscale 穿透，实现浏览器远程真机调试"]
    end

    subgraph Board["OpenVela AP 目标板端 (Allwinner F1C100s @ 32MB DDR)"]
        subgraph Apps["亮点业务与板端 APP (Application Layer)"]
            APP_UI["🖥️ UI 渲染应用<br/>• 240x240 ST7789 LCD 彩屏<br/>• Framebuffer 显存实时刷新<br/>• 硬件状态监控 / 复古终端 UI"]
            APP_AUDIO["🎵 音频语音应用<br/>• 片上 Audio Codec 驱动<br/>• 扬声器音频播报 / 麦克风录音<br/>• 系统提示音 / 交互音效"]
            APP_NET["🌐 网络与远程调试应用<br/>• USB RNDIS 虚拟网卡<br/>• LwIP TCP/IP 协议栈<br/>• 轻量 HTTP/HTTPS 服务"]
            APP_CONSOLE["💻 多路交互控制台<br/>• CDC ACM USB 虚拟串口 (/dev/ttyACM0)<br/>• ADB 调试服务 (远程传输 / Shell)<br/>• UART0 硬件串口调试通道"]
            APP_OTA["⚡ 免拆机无感 OTA 组件<br/>• 软件触发 reboot bootloader<br/>• 配合 BL 实现秒级固件热切换"]
        end

        subgraph OS["OpenVela / NuttX 操作系统底座"]
            VFS["虚拟文件系统 (VFS) / TF 卡 FAT32 挂载 (/dev/mmcsd0)"]
            USB_STACK["USB Composite 复合设备协议栈 (ADB + CDC + RNDIS)"]
        end

        subgraph Drivers["SoC 芯片级驱动层 (vendor/allwinner/chips/f1c100s/)"]
            DRV_CORE["CCU 时钟 / MMU 页表 / IRQ 中断 / DMA 控制器"]
            DRV_PERIPH["SPI / I2C (PE11/PE12) / SDIO / LRADC 按键 / PWM 呼吸灯 / 看门狗"]
        end
    end

    %% 联动关系
    Tools -- "USB Fastboot / xfel" --> Board
    Tools -- "串口 / 终端捕获" --> APP_CONSOLE
    AWBOOT -.->|"提供第一阶段引导二进制"| Board
    V86 <== "Tailscale 虚拟局域网 / 串口流" ==> APP_NET
    V86 <== "Web 终端交互" ==> APP_CONSOLE
    Apps --> OS
    OS --> Drivers

    style ExtRepos fill:#fff2e6,stroke:#ff8c00,stroke-width:2px
    style Apps fill:#e6ffe6,stroke:#00aa00,stroke-width:2px
    style Host fill:#f0f0f5,stroke:#666699,stroke-width:2px
```

---

## 图 3：免按键无感热刷与容灾回退时序图

体现本作品最具创新性的**“AI 远程写码 → 自动热刷 → 设备秒级重启生效”**研发闭环：

```mermaid
sequenceDiagram
    autonumber
    participant Dev as 开发者 / AI Agent
    participant Host as 宿主机 (flash-ap-fastboot.sh)
    participant AP as F1C100s AP (OpenVela 运行时)
    participant BL as 二级 Bootloader (Fastboot 模式)
    participant Flash as SPI-NOR Flash (0x0B1000)

    Note over Dev,AP: 阶段一：在线运行与触发重刷
    Dev->>Host: 编译生成新版 AP 固件 nuttx.bin
    Host->>AP: 通过 ADB / 串口发送 "reboot bootloader"
    AP->>AP: 写入 BootKV reboot_reason=1 并复位 CPU
    
    Note over AP,BL: 阶段二：BL 抓取升级窗口 (无需戳 FEL 按键)
    AP->>BL: 硬件重启，awboot 引导进入 BL
    BL->>BL: 读取 BootKV，检测到 reboot_reason=1
    BL->>Host: 初始化 USB MUSB，枚举为 Android Fastboot 设备
    
    Note over Host,Flash: 阶段三：静默擦写与数据落盘
    Host->>BL: fastboot flash ap nuttx.bin (仅 4 秒)
    BL->>Flash: 擦除 0x0B1000 并写入新固件镜像
    BL->>BL: 清除 reboot_reason，更新 BootKV CRC
    Host->>BL: fastboot reboot
    
    Note over BL,AP: 阶段四：热启动进入新固件
    BL->>AP: 校验 AP 镜像头，跳转到 0x80000000
    AP->>AP: OpenVela 新固件启动，ST7789 屏幕点亮
    AP-->>Host: CDC / ADB 控制台上线，Agent 接收到启动成功 Banner
```
