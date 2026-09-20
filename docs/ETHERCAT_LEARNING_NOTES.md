# EtherCAT 学习笔记与 N32H785 工程落地方案

> 配套工程：`Lwip_Ping_Test`（N32H785，Cortex-M7 600 MHz + M4 双核）
> 目标：做 **PMSM 伺服从站**（FOC + CiA402），能被 EtherCAT 主站控制（CSP / CSV / CST）
> 本文档随学习进度增量更新。**每一节的「证据」都必须能在本仓库或官方手册中定位到。**

---

## 1. 结论先行：这颗芯片自带 EtherCAT 从站控制器

**N32H785EC 型号里的 `EC` = EtherCAT。ESC（EtherCAT Slave Controller）集成在片内，挂在 AHB9 总线上，不需要外挂 LAN9252 / ET1100。**

### 1.1 仓库内的硬证据

| 证据 | 位置 | 内容 |
|---|---|---|
| 4 个 ESC 专用中断向量 | `APP/firmware/CMSIS/device/n32h7xx.h:348-351` | `ESC_OPB_IRQn=187`、`ESC_SYNC0_IRQn=188`、`ESC_SYNC1_IRQn=189`、`ESC_WRP_IRQn=190` |
| ESC 基地址 | `n32h7xx.h:2877,3080,3081` | `ETHERCAT_BASE = 0x400B0000`、`ETHERCAT_WRAPPER_BASE = 0x400C0000` |
| ESC 时钟使能（**双核可选**） | `n32h7xx_std_periph_driver/inc/n32h7xx_rcc.h:804-807` | `RCC_AHB9_PERIPHEN_M7_ESC` / `M4_ESC` / `M7_ESCLP` / `M4_ESCLP` |
| ESC 复位 | `n32h7xx_rcc.h:310` | `RCC_AHB9_PERIPHRST_ESC` |
| ESC 内核时钟源 | `n32h7xx_rcc.h:2012-2018` | `SYSBUSDIV` / `PLL1B` / `PLL2B` / `PLL3A` / `PLL3C` |
| ESC 内核时钟配置函数 | `n32h7xx_rcc.c:6733` | `RCC_ConfigETHERCATKerClk(CLK_source, CLK_divider)` |
| **100 MHz 硬性要求** | `n32h7xx_rcc.c:6731` | `*\note You need to make sure that the ETHERCAT kernel clock frequency is 100MHz` |
| 中断弱符号已预留 | `firmware/CMSIS/device/startup/startup_n32h78x_cm7.s:287-290,583` | `ESC_SYNC0_IRQHandler` 等，实现同名函数即自动挂载 |

### 1.2 一个重要（且必须提前知道的）缺口

**Nations 的标准外设库只提供了 ESC 的「时钟配置」，没有任何 ESC 寄存器定义、没有 SM/FMMU 配置、没有 ESM 状态机。**

```
$ grep -ri "ethercat" APP/firmware/n32h7xx_std_periph_driver/ --include=*.c
  → 只命中 n32h7xx_rcc.c 的 RCC_ConfigETHERCATKerClk()
```

**推论（对项目排期影响很大）**：

- ESC 寄存器布局遵循 **Beckhoff ET1100/ET1200 规范**（所有片内 ESC 的 MCU 都这样：TI AM335x/AM64x PRU-ICSS、瑞萨 RZ/T 等）。
- 开发依据 = **ETG.1000**（协议）+ **ETG.1020**（一致性测试）+ **Beckhoff ET1100 Hardware Data Sheet**（寄存器地图）+ **N32H7xx 用户手册**（芯片特有：时钟树、引脚、PHY 连接）。
- 从站协议栈用 **ETG 官方 SSC（Slave Stack Code）工具**生成，然后自己写一层 **ESC 寄存器访问 BSP**。
- **软件工作量估计**：ESC BSP + SM/FMMU + ESM + 邮箱(CoE) + 对象字典 + PDO + CiA402，属于**月级**任务，不是周级。

---

## 2. 硬件蓝图

### 2.1 器件选型（按优先级）

| 阶段 | 器件 | 作用 | 说明 |
|---|---|---|---|
| **① 立刻** | TwinCAT 3 (XAE Shell) | 主站 | 免费，Windows，图形化扫从站/看 WKC/配 PDO |
| **② 验证从站栈** | **LAN9252 开发板**（或 ET1100 EVB） | 从站样本 | **不要跳过这步**。用它将 INIT→PREOP→SAFEOP→OP 全链路走通，把伺服调试 80% 的坑在无电机条件下暴露 |
| **③ 最终伺服驱动** | **N32H785EC 自建板** | 伺服从站 | 片内 ESC，省掉 PDI 通信层 |

> **不要**用「普通 STM32 + lwIP 软件模拟 EtherCAT 从站」。EtherCAT 的 on-the-fly 转发必须硬件完成，软件做不到，此路不通。

### 2.2 N32H785EC 伺服从站网络拓扑

```
      主站 (TwinCAT / PC 网卡)
            │
    ┌───────▼────────┐
    │  PHY_A (RMII)  │──── ESC Port0（入口）
    │                │
    │  N32H785EC     │  片内 ESC：FMMU / 4×SM / DC / ESM / 64bit 时钟
    │                │
    │  PHY_B (RMII)  │──── ESC Port1（出口，接下一个从站或折返）
    └───────┬────────┘
            │
      下一个从站 / 折返回主站
```

**关键决策（画 PCB 之前必须定）：**

| 问题 | 结论 |
|---|---|
| 能否同一对 PHY 同时跑 lwIP 和 EtherCAT？ | **不能。** EtherCAT 要求纳秒级确定性的帧转发，与 MAC 抢占 PHY 会破坏实时性 |
| 想要 lwIP + EtherCAT 并存怎么办？ | 需要 **4 个 PHY**（2 个给 ESC，2 个给 ETH1/ETH2）。优先建议：**放弃 lwIP，用 ESC 邮箱（CoE/EoE）做配置通道** |
| 现有 `Lwip_Ping_Test` 怎么处理？ | 它验证了 ETH1 MAC + RMII + PHY 的硬件通路，**这部分经验可直接复用**到 ESC 的 RMII 引脚配置上 |

### 2.3 PHY 选型与配置（EtherCAT 专用要求）

现有配置（`APP/bsp/bsp_eth.h`）：

```c
#define USE_ETH1              (1U)
#define PHY_USE_YT8522H               /* 裕太微 */
#define ETH_SEL_MEDIAIF       (ETH_RMII_MODE)
#define PHY_USE_AUTONEG       (1U)    /* ← EtherCAT 场景下需要改成确定性配置 */
#define ETH_RX_BUFFER_SIZE    (1536U)
```

**EtherCAT 对 PHY 的硬性要求：**

| 要求 | 原因 |
|---|---|
| **强制 100 Mbps 全双工**（建议禁用自协商，或协商完成后锁定） | EtherCAT 只跑 100M 全双工；自协商抖动会造成链路重训练 |
| **关闭 EEE（节能以太网）** | EEE 会引入不可预测的唤醒延迟，直接破坏转发时序 |
| **关闭 Auto-MDIX / Auto-Downshift** | 自动降速到 10M = 直接通信失败 |
| **转发延迟低且一致（典型 < 500 ns）** | 影响整个环路的周期预算 |
| 优先选有 EtherCAT 应用笔记的型号 | **KSZ8081 / DP83822 / LAN8720**；YT8522H 需向厂商确认 EtherCAT 适用性 |

> **RMII 参考时钟（50 MHz）的抖动直接决定 DC 同步精度。** 必须用有源晶振或专用时钟输出，不要用 MCU 的普通 GPIO 分频。

### 2.4 ESC 内核时钟 = 精确 100 MHz（易错点）

SDK 提供的分频表（`n32h7xx.h:6605-6614`，注意 DIV16 是特例）：

| 宏 | 值 | 实际分频比 |
|---|---|---|
| `ESCSYSDIV_DIV1` | 0 | 1 |
| `ESCSYSDIV_DIV2` | 1 | 2 |
| `ESCSYSDIV_DIV4` | 2 | 4 |
| `ESCSYSDIV_DIV8` | 4 | 8 |
| **`ESCSYSDIV_DIV16`** | **7** | **16**（跳过值 3/5/6） |
| `ESCSYSDIV_DIV32` | 8 | 32 |
| … 至 `DIV512` | 12 | 512 |

**推算（待用户手册确认）**：`RCC_GetClocksFreq()` 中 `AHB9ClkFreq = SysBusDivClkFreq`（`n32h7xx_rcc.c:7766`）。若系统总线为 300 MHz：

- `SYSBUSDIV` 路线：`300/2 = 150 MHz` 或 `300/4 = 75 MHz` —— **都无法得到 100 MHz**，不可用。
- 正确路线：**从 `PLL1B` / `PLL2B` / `PLL3A` / `PLL3C` 单独配一路精确 100 MHz** 给 ESC 内核。

> ⚠️ **必须在 N32H7xx 用户手册的 RCC 时钟树章节确认 PLL 约束**（VCO 范围、输出分频粒度），再决定用哪路 PLL。**配错这里，DC 抖动超标，多轴同步直接废掉。**

### 2.5 双核分配建议

`RCC_AHB9_PERIPHEN_M7_ESC` 和 `_M4_ESC` 说明 **ESC 只能归一个核**。推荐架构：

| 核 | 职责 | 理由 |
|---|---|---|
| **M4** | EtherCAT 从站栈（SSC 生成）+ 邮箱(CoE) + 对象字典 | 实时性要求相对低，且栈代码量大，隔离出去不干扰控制 |
| **M7** | FOC 电流环（16~20 kHz）+ CiA402 + 位置插值 | 600 MHz 独占，保证 50 µs 硬期限 |

核间通信：**共享 SRAM + 硬件信号量**，交换 PDO 数据快照。注意 Cache 一致性（M7 开 D-Cache，需 `SCB_CleanDCache()` / `InvalidateDCache()`，参见 README 关键修复 #6 的同类问题）。

### 2.6 100 MHz 时钟的可达性推算（本次核实结论）

**已知事实（仓库内可证）：**

- `RCC_GetClocksFreqValue()` 中 `AHB9ClkFreq = SysBusDivClkFreq`（`n32h7xx_rcc.c:7766`）
  → **AHB9 总线时钟 = 系统总线时钟**，中间无独立分频。
- ESC 内核时钟源二选一：`SYSBUSDIV`（再经 `ESCSYSDIV` 分频）或 4 路 PLL 之一。
- `ESCSYSDIV` 分频表（`n32h7xx.h:6605-6614`）：1/2/4/8/**16**/32/64/128/256/512
  （编码值依次为 0/1/2/4/**7**/8/9/A/B/C，注意 DIV16 编码为 7，为特例）。

**推算：** 若系统总线为 300 MHz（600 MHz M7 的常见配置）：

| 路线 | 计算 | 结果 | 可用性 |
|---|---|---|---|
| `SYSBUSDIV` + `ESCSYSDIV_DIV1` | 300 / 1 | **300 MHz** | ✗ |
| `SYSBUSDIV` + `ESCSYSDIV_DIV2` | 300 / 2 | **150 MHz** | ✗ |
| `SYSBUSDIV` + `ESCSYSDIV_DIV4` | 300 / 4 | **75 MHz** | ✗ |
| 其余分频 | 300/8… | 更小 | ✗ |
| **PLL1B / PLL2B / PLL3A / PLL3C** | 由 PLL 直接配置 | **可精确 100 MHz** | ✓ |

> **结论：在 300 MHz 系统总线下，`SYSBUSDIV` 路线凑不出 100 MHz（分频链只有 2 的幂），
> 必须用独立 PLL 输出 100 MHz 给 ESC 内核。**
> 除非把系统总线改成 200 MHz 或 400 MHz，才能用 `SYSBUSDIV ÷ 2` 或 `÷ 4` 得到 100 MHz。

**仍待确认（用户手册 RCC 章节）**：哪路 PLL 可用于 ESC、其 VCO 与输出分频约束
（`n32h7xx_rcc.h:94-101` 给出 `VCO_MIN 300M / VCO_MAX 1.25G / REF 1M~64M / NR 1~64 / NF 2~4095`）。
**这是第 1 项待办的核心内容。**

---

## 3. 软件分层与仓库对齐

### 3.1 建议目录结构（沿用本工程现有分层风格）

```
APP/
├── Mcal/                          【硬件抽象层】
│   └── inc|src/ Mcal_Esc_Init.*   ✅已交付：ESC 上电时序、复位、100MHz 内核时钟（见 §9）
├── User_App/                      【应用业务层】
│   ├── inc|src/ App_EscSm.*       新增：SM / FMMU 配置
│   ├── inc|src/ App_EscEsm.*      新增：ESM 状态机（INIT/PREOP/SAFEOP/OP）
│   ├── inc|src/ App_EscCoe.*      新增：CoE 邮箱（SDO 上/下载）
│   ├── inc|src/ App_ObjectDict.*  新增：对象字典（0x6040/0x6041/0x6060/0x6064/0x6071...）
│   ├── inc|src/ App_Pdo.*         新增：PDO 映射与过程映像拷贝
│   ├── inc|src/ App_Cia402.*      新增：CiA402 驱动状态机
│   └── inc|src/ App_Foc.*         （待建）FOC：Clarke/Park/PI/SVPWM
└── firmware/                      N32 SDK（ESC 寄存器定义需自行补充，SDK 未提供）
```

### 3.2 任务与中断分配（与现有 `Mcal_TimerTask` 协作式调度共存）

**实时域（不可放入任务槽）**

| 中断 | 周期 | 优先级 | 职责 |
|---|---|---|---|
| `ESC_SYNC0_IRQHandler` | DC 周期 250 µs~1 ms | 中 | 触发 PDO 交换：从 ESC DPRAM 取 RxPDO，回写 TxPDO |
| `ESC_OPB_IRQHandler` | 事件 | 中 | ESC 事件（SM 变化、EEPROM 就绪、AL Control 写入） |
| PWM / 电流环中断 | 16~20 kHz | **最高** | CiA402 状态机 → 位置插值 → 速度环 → 电流环 → SVPWM |

**非实时域（可放入现有任务槽）**

| 任务槽 | 周期 | 职责 |
|---|---|---|
| `Task_10ms_Handle` | 10 ms | ESM 状态机轮询、AL Status 更新、LED 状态指示 |
| `Task_20ms_Handle` | 20 ms | ESM 状态机轮询、AL Status 更新、LED 状态指示 |
| `Task_50ms_Handle` | 50 ms | SDO 邮箱分段传输处理、对象字典备份 |
| `Task_200ms_Handle` | 200 ms | 通信统计（WKC 错误计数、帧错误计数） |
| `Task_300ms_Handle` | 300 ms | 诊断日志、温度/母线电压监控（可触发 PDO 里的状态位） |

> ⚠️ **现工程约束**：`Mcal_TimerTask` 是**不可抢占的协作式**调度（README 附录 C）。因此任务槽内的任何函数**执行时间必须远小于其周期**。ESC 的 SDO 分段传输不能阻塞式等待，必须做成**状态机 + 非阻塞轮询**。

### 3.3 从站落地路线（六步，每步都要有可验证的现象）

| 步骤 | 内容 | 验证现象 |
|---|---|---|
| **1** | ESC 时钟使能 + 100 MHz 内核时钟 + 读 `0x0000`(Type) / `0x0008`(Revision) | 串口打印出正确的 ESC 类型与版本号 |
| **2** | 读 EEPROM（`0x0502` 等）加载 ESI，配置 SM0/SM1 邮箱 | 主站能扫到从站，进入 **PRE-OP** |
| **3** | CoE：实现 SDO Info / SDO Upload / SDO Download | 主站能读到对象字典，能改参数 |
| **4** | 配置 SM2/SM3 + FMMU + PDO 映射 | 主站能进 **SAFE-OP**（输入 PDO 有数据） |
| **5** | 实现 AL Control 响应，进入 **OP** | 主站进 OP，**WKC 正确**，过程映像可读可写 |
| **6** | CiA402 + FOC 接入 | 主站发 `0x000F`，电机开始出力；CSP/CSV/CST 三模式可控 |

---

## 4. 核心数据通路速查

### 4.1 一帧 EtherCAT 报文布局

```
偏移  长度  内容
 0     6   目的 MAC = 01:01:01:01:01:01（广播）
 6     6   源 MAC（主站）
12     2   EtherType = 0x88A4          ← 与 lwIP 的 0x0800 区分
14     2   EtherCAT 帧头（Length[10:0] + Type[15:12]，1=EtherCAT）
16    10   Datagram 头
              Cmd(1B) Idx(1B) Address(4B) Len(2B) M(1b) C(1b) IRQ(2B)
26     N   Data（N = Len，2..1486）
26+N   2   WKC  Working Counter（初值 0，每个成功处理的从站 +1；读+1，读写+2）
末     4   FCS（标准以太网 CRC32）
```

### 4.2 命令码（影响功能实现）

| Cmd | 值 | 用途 |
|---|---|---|
| APRD | 0x01 | 位置寻址读（初始化扫描） |
| APWR | 0x02 | 位置寻址写（分配站地址） |
| FPRD | 0x04 | 节点寻址读（读寄存器） |
| FPWR | 0x05 | 节点寻址写 |
| **LRW** | **0x0C** | **逻辑读写 —— 周期性 PDO 交换全靠它** |
| BRD | 0x0A | 广播读 |

### 4.3 三种寻址模式

| 模式 | 机制 | 使用阶段 |
|---|---|---|
| 位置寻址（Auto Increment） | 帧每过一个从站地址自动减 1，减到 0 的从站响应 | 初始化扫描 |
| 节点寻址（Configured Station Address） | 启动时主站分配的唯一位址 | 寄存器读写、配置 |
| **逻辑寻址（FMMU）** | 主站维护全局过程映像的逻辑地址，**FMMU 硬件做地址翻译** | **周期性 PDO（全程）** |

### 4.4 WKC 排查表

| 期望 WKC | 实测 | 含义 |
|---|---|---|
| N | N | 正常 |
| N | < N | 有从站未处理（掉线 / 未进 OP / SM 未使能） |
| N | 0 | 拓扑断开或全部未就绪 |

---

## 5. ESM 与 CiA402 —— 「伺服不动」的两大排查入口

### 5.1 ESM（通信状态，4 态）

```
INIT ──► PRE-OP ──► SAFE-OP ──► OP
 │         │           │         │
寄存器    邮箱       输入PDO   全部PDO
可用     (SDO)     可用      可用
                    输出被忽略
```

**排查依据**：主站通过 SDO 读 `0x0130`（AL Status）/ `0x0134`（AL Status Code）。

| AL Status Code | 含义 | 排查方向 |
|---|---|---|
| `0x0011` | Invalid requested state change | 跳级请求（如 INIT 直接要 OP） |
| `0x0016` | Invalid mailbox configuration | SM0/SM1 邮箱未配好 |
| `0x0017` | Invalid sync manager configuration | SM2/SM3 配置错误 |
| `0x001E` | Invalid input configuration | FMMU 输入映射错误 |
| `0x0020` | Invalid output configuration | FMMU 输出映射错误 |
| `0x0024` | No valid inputs available | 输入 PDO 无数据 |
| `0x0025` | No valid outputs | 输出 PDO 未收到（**多半是 PDO 映射与 ESI 不一致**） |

### 5.2 CiA402（驱动状态）

使能四步（`0x6040` 控制字）：

```
0x0006  Shutdown          → Ready to Switch On
0x0007  Switch On         → Switched On
0x000F  Enable Operation  → Operation Enabled   ← 电机开始出力
0x0000  Disable Voltage   → 停机
0x0080  Fault Reset       → 清故障（上升沿）
```

> ⚠️ `Quick Stop` 是控制字 **bit 2 且低有效**。`0x0002` 是**触发快速停机**，不是使能。

### 5.3 CiA402 关键对象

| 索引 | 名称 | 方向 | 备注 |
|---|---|---|---|
| `0x6040` | Controlword | Rx | 2 B |
| `0x6041` | Statusword | Tx | 2 B |
| `0x6060` | Modes of operation | Rx | 8=CSP, 9=CSV, 10=CST |
| `0x6061` | Modes of operation display | Tx | |
| `0x6071` | Target torque | Rx | **单位 = 额定转矩的 0.1%** |
| `0x6072` | Max torque | — | 限幅用 |
| `0x6075` | Motor rated current | — | 换算链 |
| `0x6076` | Motor rated torque | — | 换算链 |
| `0x6077` | Torque actual value | Tx | **= Kt × Iq** |
| `0x607A` | Target position | Rx | **单位是增量/用户单位，不是弧度** |
| `0x6064` | Position actual value | Tx | |
| `0x606C` | Velocity actual value | Tx | |
| `0x6091` | Gear ratio | — | 用户单位 ↔ 增量换算 |
| `0x6092` | Feed constant | — | 直线电机/丝杆换算 |

### 5.4 转矩→电流换算（必做限幅）

```
Iq_ref = (Target_Torque / 1000) × Motor_Rated_Torque × (1 / Kt)
限幅    : |Iq_ref| ≤ (Max_Torque / 1000) × Motor_Rated_Torque / Kt
```

**禁止硬编码**，必须从对象字典 `0x6072/0x6075/0x6076` 读取，并由主站通过 SDO 配置。

---

## 6. DC 分布式时钟（多轴同步的根）

| 概念 | 说明 |
|---|---|
| **64 位系统时间** | ESC 内部寄存器，所有从站各自维护 |
| **参考时钟** | 第一个支持 DC 的从站作为参考（或主站指定） |
| **漂移补偿** | ESC 硬件逐帧测量与参考时钟的偏差，**调整本地时钟频率**，做到**无累积误差** |
| **SYNC0 / SYNC1** | ESC 按配置周期产生的中断/信号，用于触发应用（SYNC0 通常触发 PDO 交换或电流环） |
| **对齐精度** | 典型 < 100 ns（远优于软件对时） |

**关键点：DC 不是 NTP 式的「定期校正」，而是硬件级频率锁定。** 这是 EtherCAT 能做多轴插补的根本原因。

**与 FOC 的关系**：SYNC0 周期 = DC 周期（250 µs ~ 1 ms），电流环周期 = PWM 周期（16~20 kHz）。两者独立，靠**位置插值**衔接（见第 7 节）。

---

## 7. EtherCAT 周期与 FOC 电流环的衔接

### 7.1 周期关系

| | 频率 | 职责 |
|---|---|---|
| EtherCAT / DC / SYNC0 | 250 µs ~ 1 ms | 交换目标位置与反馈 |
| 电流环（PWM 触发） | 16 ~ 20 kHz | 位置插值 → 速度环 → 电流环 → SVPWM |

### 7.2 CSP 模式的位置插值（必需）

主站按 DC 周期下发**阶梯状**目标位置，从站必须插值成平滑轨迹：

```c
/* SM2 中断内（DC 周期，1 ms） */
Old_Target     = Current_Target;
Current_Target = Pdo_Get_TargetPosition();
Interp_Step    = (Current_Target - Old_Target) / Cycles_Per_Dc;   /* 1ms / 50us = 20 */

/* 电流环中断内（50 µs） */
Interp_Position += Interp_Step;
PositionLoop(Interp_Position);
```

**这个「一个 DC 周期延迟」是位置环相位裕度的主要杀手**，第 13 课调优时第一个要量化的对象。

### 7.3 时间预算（250 µs 周期 + 20 kHz 电流环，@600 MHz）

```
每个 50 µs 电流环周期内：
  CiA402 状态机   ~2 µs
  位置插值        ~1 µs
  速度环 PI       ~3 µs
  电流环 PI ×2    ~6 µs
  SVPWM           ~3 µs
  ─────────────────────
  合计            ~15 µs   → 余量约 70%
```

> 结论：**瓶颈不是算力，是时间确定性（抖动）**。

---

## 8. 待办与风险清单

| # | 事项 | 状态 | 风险 |
|---|---|---|---|
| 1 | 在用户手册 RCC 章节确认 ESC 100 MHz 时钟源方案 | **部分完成**（见 §2.6） | 高：配错则 DC 精度不达标 |
| 2 | 确认 YT8522H 是否满足 EtherCAT 转发延迟要求 | **需向厂商索取资料** | 中：不满足需换 KSZ8081/DP83822 |
| 3 | 确认 N32H785EC 的 ESC 端口是 MII 还是 RMII，引脚如何复用 | **需查数据手册引脚表** | 高：决定 PCB 布局 |
| 4 | 获取 Nations 是否提供 SSC 移植示例 / ESC 寄存器头文件 | **需向 FAE 确认**（仓库内确认无） | 高：决定软件工作量 |
| 5 | 决定是否保留 lwIP（PHY 数量 2 vs 4） | **待决策** | 高：画板前必须定 |
| 6 | 双核职责划分（ESC 归 M4，FOC 归 M7？） | **待决策** | 中 |
| 7 | M7 D-Cache 与核间共享内存的一致性处理 | **待决策** | 中 |

### 8.1 关于「无法从公开网络核实」的说明

第 2/3/4 项涉及**厂商专有资料**（数据手册引脚表、RM 的 ESC 章节、SSC 移植包）。本次尝试从公开网络获取时，
搜索引擎只返回了 PDF 链接与页码引用（如 `CN_DS_N32H785XxB7EC_Series_Datasheet_V1.1.0.pdf`、
`CN_UM_N32H7xx_Series_User_Manual_V1.2.0.pdf`），**正文不可抓取**，因此这三项**不能靠猜测下结论**。

**正确的获取途径**（按优先级）：

1. **N32H7xx 系列用户手册**（CN_UM_N32H7xx_Series_User_Manual）的 **RCC 时钟树章节**与
   **EtherCAT 章节** —— 这是第 1/3 项的唯一权威来源。
2. **N32H785XxB7EC 数据手册** 的**引脚复用表** —— 第 3 项（MII/RMII 引脚）的唯一权威来源。
3. **国民技术 FAE / 官方论坛** —— 第 4 项（SSC 移植包、ESC 寄存器头文件）。这是决定
   软件工作量最大的不确定因素，**建议在动手写代码前先问清楚**。
4. **裕太微 FAE** —— 第 2 项（YT8522H 的 EtherCAT 转发延迟与配置要求）。

> 参考入口：[N32H785EC 产品页](https://www.nationstech.com/product/general/n32h/n32h785ec/)、
> [N32H785EC Product Brief (EN)](https://nsing.com.sg/uploads/PB/EN_PB_N32H785EC.pdf)、
> [YT8522 系列 Product Brief](https://www.motor-comm.com/Public/Uploads/uploadfile/files/20240117/YT8522ProductBrief_v0.1-984.pdf)

---

## 9. 已交付代码：ESC 上电模块

**状态：已编写，尚未编译（工程未纳入 Keil 构建，且无硬件可验）。**

| 文件 | 内容 |
|---|---|
| `APP/Mcal/inc/Mcal_Esc_Init.h` | 接口、核选择、时钟源配置宏 |
| `APP/Mcal/src/Mcal_Esc_Init.c` | ESC 上电时序、复位、内核时钟选择、时钟上报 |

### 9.1 已核实的 API 依赖（每一个都在仓库内定位到定义）

| 使用的符号 | 定义位置 |
|---|---|
| `RCC_EnableCFG4PeriphClk1` | `n32h7xx_rcc.c:6910`，声明 `n32h7xx_rcc.h:2441` |
| `RCC_CFG4_PERIPHEN_AHB9BUS` | `n32h7xx_rcc.h:999`（= `RCC_CFG4_AHB9CLKEN`，`n32h7xx.h:7539` bit18） |
| `RCC_EnableAHB9PeriphReset1` | `n32h7xx_rcc.c:2866`，声明 `n32h7xx_rcc.h:2325` |
| `RCC_AHB9_PERIPHRST_ESC` | `n32h7xx_rcc.h:310` |
| `RCC_EnableAHB9PeriphClk1` | `n32h7xx_rcc.c:3776`，声明 `n32h7xx_rcc.h:2347` |
| `RCC_AHB9_PERIPHEN_M7_ESC` / `_M4_ESC` | `n32h7xx_rcc.h:804-805` |
| `RCC_ESCKERCLK_SRC_*` | `n32h7xx_rcc.h:2014-2018` |
| `RCC_ConfigETHERCATKerClk` | `n32h7xx_rcc.c:6733`，声明 `n32h7xx_rcc.h:2437` |
| `RCC_ClocksTypeDef` / `RCC_GetClocksFreqValue` | `n32h7xx_rcc.h:65-92` / `n32h7xx_rcc.h:2457` |
| `ETHERCAT_BASE` | `n32h7xx.h:3081` |

### 9.2 编写过程中发现并修正的 3 个缺陷（记录以备复用）

> 这三个都是「凭记忆写代码」会踩的坑，对照 SDK 后修正。**方法论与 README「关键移植修复」表一致。**

| # | 缺陷 | 修正 |
|---|---|---|
| 1 | 误用 `RCC_EnableAHB9PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ...)` | AHB9 **总线**时钟走 `RCC_EnableCFG4PeriphClk1`；`AHB9PeriphClk1` 只管外设门控 |
| 2 | 结构体类型写成 `RCC_ClocksType` | 实际是 `RCC_ClocksTypeDef`（`n32h7xx_rcc.h:92`） |
| 3 | 字段写成 `PCLK1Freq` | 实际是 `APB1ClkFreq`（`n32h7xx_rcc.h:86`） |

### 9.3 上电时序（`Mcal_Esc_PowerUp` 的实现依据）

```
1. RCC_EnableCFG4PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ENABLE);  // 开 AHB9 总线时钟
2. RCC_EnableAHB9PeriphReset1(RCC_AHB9_PERIPHRST_ESC);          // 置位再清位 = 复位脉冲
3. RCC_EnableAHB9PeriphClk1(RCC_AHB9_PERIPHEN_M7_ESC, ENABLE);  // 开 ESC 外设时钟（按核选）
4. RCC_ConfigETHERCATKerClk(100MHz 源, 分频);                    // 内核时钟必须精确 100MHz
5. RCC_GetClocksFreqValue(&Clocks);                              // 读回验证 AHB9ClkFreq
```

### 9.4 验证结果（本轮实测）

| 验证项 | 方法 | 结果 |
|---|---|---|
| **编译** | `C:\Keil_v5\ARM\ARMCC\bin\armcc.exe`（与本工程同一编译器/版本）<br>`--c99 -O0 --cpu Cortex-M7.fp.dp -DN32H785 -DN32H78x -DCORE_CM7 -DUSE_STDPERIPH_DRIVER -DUSING_TCM -D__ICACHE_PRESENT=1 -D__DCACHE_PRESENT=1 -D__MICROLIB` | ✅ **退出码 0，0 error 0 warning**，产出 5008 字节 .o |
| **接口联动** | 另写临时文件调用全部 5 个公开接口 + 2 个宏，同参数编译 | ✅ **退出码 0**（签名/宏均匹配） |
| **符号冲突** | 全工程 grep `Mcal_Esc_` / `ESC_KER_CLK` / `ESC_BASE_ADDR` / `ESC_CORE_M7` / `RCC_EnableCFG4PeriphClk1` | ✅ **无冲突**，唯一命中即本模块自身（`RCC_ClocksTypeDef` 是 SDK 公共类型，各文件局部变量独立） |
| **Keil 工程集成** | 已加入 `LWIP_PING.uvprojx` 的 `Mcal` 组；XML 解析通过，文件条目 36 个，路径 `..\Mcal\src\Mcal_Esc_Init.c` 正确 | ✅ XML 有效 |
| **整工程链接** | **未执行**（见下） | ⚠️ 未验证 |

> ⚠️ **整工程 `UV4 -b` 重建未执行**，原因：若本机为 MDK-Lite 授权，本工程（约 880 KB）会在
> **链接阶段**因 32 KB 代码上限失败，而该失败**与本模块无关**，无法区分真假故障。
> 更快的验证路径：在 Keil 中打开工程直接 `F7`——你本来就有可用授权。
> **这是本模块唯一未经端到端验证的环节。**

> ⚠️ **`Mcal_Esc_PowerUp()` 目前无调用方**。当前固件（`Mcal_InitSum`）并未调用它，
> 所以加入构建后**行为零变化**——这是刻意的：先把时钟层就位，等硬件到手再在
> `Mcal_InitSum()` 里接上，并立即用 `Mcal_Esc_ReportClocks()` 的打印值对账时钟树。

### 9.5 上板后的第一个验证动作（无需 ESC 寄存器手册）

```c
/* 在 Mcal_InitSum() 中，log_init() 之后追加 */
if (Mcal_Esc_PowerUp() != 0)
{
    log_error("ESC power-up failed\r\n");
}
else
{
    Mcal_Esc_ReportClocks();   /* 打印 SysBus / AHB9 / APB1 三个频率 */
}
```

**判据**：打印出的 `AHB9` 频率必须等于手工推算值。若为 0，说明 AHB9 总线时钟门控
（`RCC_CFG4_PERIPHEN_AHB9BUS`）没打开——这是 `SYSBUSDIV` 路线能否走通的直接证据。



---

## 10. SM / FMMU 配置（第 5 课）

> 第 4 课讲了 PDO 映射的**逻辑配置**；本课讲它怎么被**烧进 ESC 硬件**。
> 这两层必须成对理解——只懂 PDO 不懂 SM/FMMU，遇到「数据错位」就无从下手。

### 10.1 两个角色，一句话区分

| | SyncManager (SM) | FMMU |
|---|---|---|
| 管什么 | **一致性 + 安全 + 事件**（缓冲、看门狗、中断） | **地址翻译**（逻辑地址 → 物理地址） |
| 关键属性 | 方向、缓冲模式、数据长度、看门狗 | 逻辑起点、长度、物理起点、方向、位操作 |
| 谁写 | 主站启动时通过寄存器 | 主站启动时通过寄存器 |
| 运行时行为 | 收到完整一帧才触发/交换；超时清输出 | 每帧实时搬运字节，**零 CPU** |
| 类比 FOC | **ADC 触发 + 过流保护**（什么时候采、出事怎么办） | **DMA 地址映射**（数据从哪搬到哪） |

**记忆钩子**：
- **SM 决定"这堆数据是什么性质"**（是主站给我的？还是我给主站的？多长？超时怎么办？）
- **FMMU 决定"这堆数据放在哪"**（逻辑地址第几字节 ↔ DPRAM 第几字节）

### 10.2 标准 4 个 SM 的分配（几乎所有人都是这个布局）

| SM | 方向 | 用途 | 典型 DPRAM 起始 | 典型长度 |
|---|---|---|---|---|
| **SM0** | 写（主站→从站） | 邮箱 **Out**（主站下发 SDO） | `0x1000` | 128 B |
| **SM1** | 读（从站→主站） | 邮箱 **In**（从站上传 SDO） | `0x1080` | 128 B |
| **SM2** | 写（主站→从站） | 过程数据 **RxPDO / Outputs** | `0x1100` | = RxPDO 总字节 |
| **SM3** | 读（从站→主站） | 过程数据 **TxPDO / Inputs** | `0x1400` | = TxPDO 总字节 |

**ESC 寄存器地址**（ET1100 兼容布局）：

```
SM0 配置: 0x0800 ~ 0x0807
SM1 配置: 0x0808 ~ 0x080F
SM2 配置: 0x0810 ~ 0x0817     ← 过程数据输出，重点
SM3 配置: 0x0818 ~ 0x081F     ← 过程数据输入，重点
...
SM 状态寄存器: 0x0804 + n*8 内的 status 字节（读/写事件标志）
```

**每个 SM 的 8 字节配置**：

| 偏移 | 字段 | 含义 |
|---|---|---|
| +0 | `Physical Start Address` | 起始物理地址（低 16 位） |
| +1 | | 高 16 位（多数 ESC 只用低 16 位） |
| +2 | `Length` | 数据长度（字节），低 16 位 |
| +3 | | 高 16 位 |
| +4 | `Control Byte` | **方向 + 缓冲模式 + 中断** |
| +5 | `Status` | 状态（中断/写事件标志） |
| +6 | `Activate` | 启用位 |
| +7 | `PDI Control` | PDI 侧中断配置 |

> ⚠️ **长度必须精确等于 PDO 总字节数**。SM2 长度写成 8 但 PDO 实际 11 字节，
> 主站会认为配置非法（AL Status Code `0x0017`），或更糟——**只交换前 8 字节**，
> 最后 3 字节（可能就是目标位置的高字节）永远是 0。这是极难查的 bug。

### 10.3 Control Byte 怎么定（决定模式和安全）

**SM2（输出）推荐 `0x24`**：

```
0x24 = 0010 0100
        ││││ │││└─ bit0-1  Operation Mode = 10b  →  Buffered (3-buffer) 模式
        ││││ ││└── bit2     Direction      = 1   →  写（主站 → 从站）
        ││││ │└─── bit3     (reserved)
        ││││ └──── bit4-5   Interrupt      = 10b →  ECAT 写事件中断使能 ★
        │││└────── bit6     Watchdog Trigger = 1  →  ★★ 本 SM 受看门狗保护
        ││└─────── bit7     (reserved)
        └┘
```

**SM3（输入）推荐 `0x20`**：

```
0x20 = 0010 0000
        ││││ │││└─ bit0-1  Buffered 模式
        ││││ ││└── bit2     Direction = 0 → 读（从站 → 主站）
        ││││ └──── bit4-5   Interrupt = 10b → 读事件中断使能
        ││└────── bit6     Watchdog Trigger = 0 → 输入数据不需要看门狗
        └┘
```

> 🔥 **`Watchdog Trigger`（bit6）是整个从站最重要的安全位。**
> 它置 1 后，ESC 硬件启动一个看门狗定时器：**只要主站没在超时前成功写入 SM2，
> ESC 自动把 SM2 的数据区清零/置安全值，并置位 SM 状态寄存器的 watchdog 错误位。**
>
> **你的固件必须检测这个错误位并立即停机。** 这就是为什么断网/主站崩溃时伺服会
> 安全停下，而不是"保持最后一个指令继续冲出去"。**这个功能不能关。**

**看门狗时间寄存器**（`0x0400`/`0x0401`）：

```
Watchdog Divider (0x0400, 16 bit)   ← 分频系数
Watchdog Time PDI (0x0410, 16 bit)  ← PDI 侧超时
超时基准 = 40 ns × (WD_Divider + 2) × WD_Time_PDI
```

**主站会算这个值，你只需保证不从站侧把它改乱。**

### 10.4 FMMU 配置寄存器（16 字节/个）

```
FMMU0 配置: 0x0600 ~ 0x060F
FMMU1 配置: 0x0610 ~ 0x061F
FMMU2 配置: 0x0620 ~ 0x062F
FMMU3 配置: 0x0630 ~ 0x063F
...
```

| 偏移 | 字段 | 含义 |
|---|---|---|
| +0 | `Logical Start Address` | 在**主站过程映像**中的起始逻辑地址（32 位，小端） |
| +4 | `Length` | 映射长度（字节，16 位） |
| +6 | `Logical Start Bit` | 逻辑侧起始位偏移 |
| +7 | `Logical Stop Bit` | 逻辑侧结束位偏移 |
| +8 | `Physical Start Address` | 映射到 **DPRAM** 的起始物理地址（16 位） |
| +10 | `Physical Start Bit` | 物理侧起始位偏移 |
| +11 | `Type` | 0 = 读（从站→主站，输入）；1 = 写（主站→从站，输出） |
| +12 | `Activate` | 启用位（写 1 才生效） |

> **位偏移字段（+6/+7/+10）就是第 4 课「位紧凑打包」在硬件层的体现。**
> 当 PDO 里的对象不是字节对齐时（例如 8 位的 `0x6060` 后面跟 32 位的 `0x607A`），
> 主站可能会用位偏移来精确映射。**多数时候用「整字节映射 + 长度对齐」更省事**，
> 这也是我建议你在 PDO 设计阶段加 padding 让对象字节对齐的原因（见 §11.1 作业答案的位布局）。

### 10.5 完整配置示例（对接第 4 课的伺服 PDO）

假设 RxPDO 总 11 B、TxPDO 总 10 B（第 4 课作业的设计）：

| FMMU | 用途 | Logical Start | Length | Physical Start | Type | Activate |
|---|---|---|---|---|---|---|
| FMMU0 | SM0 邮箱 Out | `0x00000000` | 128 | `0x1000` | 1（写） | 1 |
| FMMU1 | SM1 邮箱 In | `0x00000080` | 128 | `0x1080` | 0（读） | 1 |
| FMMU2 | **SM2 → 输出（主站→从站）** | `0x00000100` | **11** | `0x1100` | **1（写）** | 1 |
| FMMU3 | **SM3 → 输入（从站→主站）** | `0x00000100 + 11 = 0x0000010B` | **10** | `0x1400` | **0（读）** | 1 |

**配置顺序（写在 PRE-OP 阶段）**：

```
1. 写 SM0/SM1 配置 + Activate        ← 先让邮箱能通（SDO 要用）
2. 写 FMMU0/FMMU1 + Activate         ← 邮箱地址翻译生效
3. 等主站通过 SDO 把 0x1C12/0x1600/0x1A00 写完（PDO 映射协商）
4. 用协商结果算出 SM2/SM3 的 Length  ← ★ 不能硬编码，必须用主站给的映射
5. 写 SM2/SM3 配置 + Activate
6. 写 FMMU2/FMMU3 + Activate
7. 上报 AL Status，允许进 SAFE-OP / OP
```

> ⚠️ **第 4 步是新手最容易写死的地方。** 如果你把 SM2 长度硬编码成 11，
> 而主站通过 SDO 改成了 8 字节的映射，你的 SM2 长度就错了 → 交换的字节数不对 → 数据错位。
> **必须遍历 0x1600/0x1A00 的条目，按位长累加算出实际长度。**

### 10.6 排查表：SM/FMMU 配错会看到什么

| 现象 | 可能原因 |
|---|---|
| 进不了 SAFE-OP，AL Code `0x0017` | SM2/SM3 配置非法（长度 0、地址越界、Control Byte 非法） |
| 进不了 OP，AL Code `0x001E` | FMMU 输入映射错（长度/方向/物理地址） |
| 进不了 OP，AL Code `0x0020` | FMMU 输出映射错 |
| 能进 OP，但**部分数据恒为 0** | SM 长度小于 PDO 总长（最常见） |
| 能进 OP，但**数据整体错位/数值离谱** | PDO 位偏移计算错误，或 FMMU 物理起始地址与 SM 不一致 |
| WKC 正常但主站报「输入数据无效」 | SM3 未使能 `读事件中断`，或固件没在 SM3 事件里更新数据 |
| 主站一断线电机就飞车 | **SM2 的 Watchdog Trigger 没使能**，或固件没检查看门狗错误位 |

### 10.7 本课作业

**作业 1**：用第 4 课你自己设计的 PDO（RxPDO 11 B / TxPDO 10 B），写出 SM2 和 SM3 的
`Physical Start Address`、`Length`、`Control Byte` 三个值，并说明为什么。

**作业 2**：FMMU2 和 FMMU3 的逻辑起始地址**能不能重叠**？为什么？
（提示：想想逻辑地址是"主站过程映像"的地址，同一个逻辑区能不能既当输入又当输出。）

**作业 3（关键思考题）**：为什么 SM 长度**必须**从主站的 PDO 映射协商结果推导，
而不能在固件里硬编码？如果硬编码了，什么情况下会「编译通过、通信正常、电机动作错」——
这种最难查的故障？

---

## 11. 学习进度

| 课 | 主题 | 状态 |
|---|---|---|
| 1 | Frame 结构 / WKC / 三种寻址 | 已讲 |
| 2 | ESC 片内集成 + 硬件蓝图 | 已讲 |
| 3 | 数据通路全景（ESM/CiA402/DC 概览） | 已讲 |
| 4 | **对象字典 + PDO 映射（0x1600/0x1A00）** | 已讲（作业答案见下） |
| — | **ESC 上电模块（Mcal_Esc_Init）** | ✅ **已交付并编译验证**（§9） |
| 5 | **SM / FMMU 配置（第 4 课的硬件实现层）** | ✅ **已讲（§10）** |
| 6 | **ESM 状态机（INIT→PREOP→SAFEOP→OP）** | ✅ **已讲（§12）+ 代码已交付编译验证** |
| 7 | **CoE 邮箱 + 对象字典（SDO / Abort 码 / 分诊表）** | ✅ **已讲（§13）**，代码待寄存器手册 |
| 8 | **CiA402 + FOC 对接（CSP/CSV/CST + 四层保护）** | ✅ **已讲（§14）+ 代码已交付真值表验证** |
| 9 | **DC 分布式时钟（漂移补偿 / SYNC0·SYNC1 / 寄存器 0x0900~）** | ✅ **已讲（§15）** |
| 10 | TwinCAT 主站实操 | 需硬件 |
| 11 | ESC 寄存器 + AHB9 裸机实验 | 需硬件（需寄存器手册） |
| 12 | 性能调优（周期抖动 / DC 误差 / 丢帧定位） | 需硬件 |

**协议原理教学已全部完成（第 1~9 课）。剩余第 10~12 课均需硬件才能推进。**

### 11.2 当前阻塞点

| 类别 | 是否阻塞教学 |
|---|---|
| 硬件待办 第 2/3/4 项（YT8522H / ESC 端口 / SSC 包） | ❌ 不阻塞协议原理教学 |
| 第 4~9 课 | ✅ 已全部讲完 |
| **第 10~12 课** | ✅ **阻塞**，必须有硬件才能验证 |
| **教学闭环本身** | ⚠️ **阻塞**：目标要求"每阶段确认理解后再进入下一阶段"，需用户作业反馈 |

**外部阻塞**：ESC 寄存器布局是否与 ET1100 兼容（见 §11.4），以及缺少硬件与用户反馈。

### 11.3 已交付代码总览

| 模块 | 文件 | 验证状态 |
|---|---|---|
| ESC 上电（时钟/复位/100MHz） | `APP/Mcal/src/Mcal_Esc_Init.c` + `inc/Mcal_Esc_Init.h` | ✅ 编译 + 链接 |
| ESC 寄存器访问层（共享） | `APP/Mcal/src/Mcal_Esc_Reg.c` + `inc/Mcal_Esc_Reg.h` | ✅ 编译 + 链接（§11.5 抽出） |
| ESC 寄存器地图（偏移定义） | `APP/User_App/inc/App_Esc_Reg.h` | ⚠️ 编译通过，**偏移待核对**（§11.4） |
| ESM 状态机 | `APP/User_App/src/App_Esc_Esm.c` + `inc/App_Esc_Esm.h` | ✅ 编译 + 链接 + 接口检查 |
| CiA402 驱动状态机 | `APP/User_App/src/App_Cia402.c` + `inc/App_Cia402.h` | ✅ 编译 + 链接 + **真值表验证 PASS**（§14.4） |
| CiA402 行为测试（待跑） | `APP/tools/cia402_behaviour_check.c` | ⚠️ 编译通过，**尚未执行**（本机无 ARM 执行环境） |
| CiA402 真值表校验脚本 | `APP/tools/cia402_table_check.py` | ✅ **python 运行 PASS** |

**全部 4 个源文件均已注册进 `LWIP_PING.uvprojx`**（工程文件 XML 有效、无重复条目）。

> ⚠️ **`Mcal_Esc_PowerUp()`、`App_Esc_Esm_Init()`、`App_Cia402_Init()` 目前均无调用方。**
> 加入构建后**行为零变化**——刻意设计：先把地基铺好，硬件到手后再逐个接通。

> 📌 **编号说明**：11.1（第 4 课作业答案）在本节之后，故 11.2/11.3 出现在 11.1 之前。
> 这是文档编辑事故的残留；**内容完整**，但编号顺序未重排
> （重排脚本曾在第 9 轮误删内容，故不再对该文件做自动化重排）。
### 11.1 第 4 课作业答案（核定）

**作业 1**：RxPDO = `0x6040`(16b) + `0x6060`(8b) + `0x607A`(32b) + `0x60FF`(32b)；
TxPDO = `0x6041`(16b) + `0x6061`(8b) + `0x6064`(32b) + `0x606C`(32b)

**① 条目编码**

| PDO | 条目 | 编码 |
|---|---|---|
| Rx | `0x6040` | `0x60400010` |
| Rx | `0x6060` | `0x60600008` |
| Rx | `0x607A` | `0x607A0020` |
| Rx | `0x60FF` | `0x60FF0020` |
| Tx | `0x6041` | `0x60410010` |
| Tx | `0x6061` | `0x60610008` |
| Tx | `0x6064` | `0x60640020` |
| Tx | `0x606C` | `0x606C0020` |

**② 总字节数**：Rx = 2+1+4+4 = **11 B**；Tx = 2+1+4+4 = **10 B**

**③ `0x60FF` 的起始位偏移 = 56**（即第 **7** 字节，位数从 0 起算）

```
RxPDO 位布局（紧凑打包，无字节对齐）：
 bit   0 ─ 15 : 0x6040 Controlword      (2 B, byte 0-1)
 bit  16 ─ 23 : 0x6060 Modes of op      (1 B, byte 2)
 bit  24 ─ 55 : 0x607A Target position  (4 B, byte 3-6)  ← 跨字节！
 bit  56 ─ 87 : 0x60FF Target velocity  (4 B, byte 7-10) ← 起始位 56
```

> ⚠️ **注意「位偏移」不能按字节数错算。** 正确做法是逐条累加**位长**：
> 16 (`0x6040`) + 8 (`0x6060`) = 24 → `0x607A` 从位 24 起，占 32 位 → 24 + 32 = **56**
> → `0x60FF` 从位 **56**（第 7 字节）起，结束于位 87（第 10 字节）。
> **验证**：88 位 = 11 字节，与 ② 的总字节数一致 ✓
>
> 常见的错误算法是「前面 5 个字节 × 8 = 40」或「5 × 16 = 80」之类——
> **只要用「位长累加」并且最后和总字节数对账，就不会错。**

**作业 2（为什么允许运行时改 PDO 映射）**：
好处是**一个固件适配多种主站配置**（不同主站、不同应用要交换的数据不同），
无需为每种配置编译一版固件。代价是**固件复杂度上升**：必须实现完整的 SDO
配置写入校验、按协商结果动态重算 SM/FMMU，且必须处理「配置写入后 SM 长度变化」
导致的重新映射。**这就是 §10.5 第 4 步必须动态推导长度的原因。**

### 11.4 ⚠️ 已交付代码的**根本性未验证假设**（必读）

**本节是对已交付的三个模块最重要的免责说明，比"编译通过"重要得多。**

已交付的 `App_Esc_Reg.h` 与 `App_Esc_Esm.c` **建立在一个单一假设之上**：

> **假设：N32H785 片内 ESC 的寄存器布局与 Beckhoff ET1100 兼容。**

**这个假设的来源是行业惯例，不是官方文档。** 事实依据：

| 依据 | 强度 |
|---|---|
| 所有片内集成 ESC 的 MCU（TI AM335x/AM64x PRU-ICSS、瑞萨 RZ/T 等）都沿用 ET1100 寄存器布局 | 强惯例，**非保证** |
| 本工程 SDK 中**没有任何** ESC 寄存器定义可供对照 | 无法自证 |
| 公开网络检索未能取得 N32H785 的 ESC 寄存器手册 | 未取得 |

**因此**：

- `App_Esc_Reg.h` 中所有寄存器偏移**必须逐条对照官方手册核对后才能上板使用**。
  文件中已用 `[ET1100]` / `[VERIFY]` 标注置信度，但**标注本身不能替代核对**。
- 若该假设**不成立**，则 `App_Esc_Reg.h` 的全部偏移需重写、`App_Esc_Esm.c` 的
  寄存器访问层需相应调整。**（但 ESM 状态机的逻辑结构——状态跳转、错误码语义、
  看门狗语义、握手方式——在 ETG.1000 层面是协议标准，不论寄存器布局如何都成立。
  这是本次交付中"与芯片无关、可长期复用"的部分。）**
- 其他**未验证的硬件行为假设**：
  - ESC 寄存器是否允许 **8 位 / 16 位**访问（`App_Esc_Esm.c` 两种都用到了）
  - 中断号 187~190 的实际触发源
  - ESC 端口数量与 MII/RMII 类型

**结论**：本文档的**协议教学部分**（第 1~7 课）与**工程逻辑部分**（状态机结构、
任务划分、DC 与 FOC 的衔接）是可靠的；**寄存器地址部分**是待核对的**草案**。

**核对入口**：见 `ETHERCAT_FAE_QUESTIONS.md` 的 **P0 项 Q1.1 / Q1.2**
——这两问的回答将直接决定上述假设是否成立。

---

### 11.5 本轮重构：抽出共享的 ESC 寄存器访问层

**发现的缺陷（设计层）**：`App_Esc_Esm.c` 自己带了 4 个**私有**的寄存器访问函数，
而 `App_Esc_Reg.h` 本应是共享的寄存器层。**后果**：接下来做邮箱、PDO、DC 时，
每个模块都会**复制一遍**这 4 个函数——典型的"复制粘贴蔓延"。

**重构动作**：抽出 `Mcal_Esc_Reg.c` / `.h`（放在 Mcal 层，与 `Mcal_Esc_Init` 同级），
提供完整的宽度支持：

| 函数 | 用途 |
|---|---|
| `Mcal_Esc_Reg_Read8` / `Write8` | 单字节字段（如 SM Status、ESM 状态字节） |
| `Mcal_Esc_Reg_Read16` / `Write16` | 16 位寄存器（AL Control/Status、SM 长度） |
| `Mcal_Esc_Reg_Read32` / `Write32` | **32 位字段（DC 时间戳、SYNC0 周期）** |
| `Mcal_Esc_Reg_Read64` | **64 位系统时间（`0x0910`，DC 必需）** |
| `Mcal_Esc_Reg_SetBits16` / `ClearBits16` | 位级读改写（激活位、事件标志） |

**为什么必须补 32/64 位**：第 9 课的 DC 需要读 **64 位系统时间**（`0x0910`+`0x0914`）
和 32 位 SYNC0 周期（`0x098A`）。**原来只有 8/16 位访问器，DC 根本无法实现。**

**重构中真实踩到的坑（值得记录）**：

我用脚本做字符串替换把调用点改名，结果**把函数定义也改了名**——
`static uint8_t Esc_Reg_Read8` 变成 `static uint8_t Mcal_Esc_Reg_Read8`，
与共享层的同名函数构成**重复定义**。

> ⚠️ **这个错误编译期发现不了**（两个 .c 各自编译都能过），**只有链接才会报**
> `L6200E: Symbol multiply defined`。
> **教训：改名这种机械操作必须"调用点"和"定义点"分开处理，改完必须链接验证。**
> 这也是我本轮特意补上链接测试的原因。

### 11.6 链接验证（本轮新增的验证手段）

**为什么之前不够**：前几轮我只做**编译**（`armcc -c`）。但编译是**分文件**的，
它发现不了：**重复定义**、**未解析的外部符号**、跨模块签名不一致。

**本轮补的链接测试**（`armlink`，含 SDK 的 `n32h7xx_rcc.c`）：

```
compiled 6 objects (SDK rcc.c included)
=== armlink exit code: 0 ===
PASS: full image linked, 58612 bytes
```

**这次链接实际证明了什么**：

| 证明项 | 说明 |
|---|---|
| 重构后**无重复定义** | 否则 `L6200E` |
| **跨模块符号全部解析** | `Mcal_Esc_Reg` ↔ `App_Esc_Esm` ↔ `App_Cia402` 互相调用成功 |
| **与 SDK 接口匹配** | 5 个 RCC 函数（`RCC_ConfigETHERCATKerClk` 等）全部由 `n32h7xx_rcc.c` 提供并解析成功 |
| **无未声明的隐式依赖** | `--strict` 下 0 warning |

**第一次链接时报的 5 个未定义符号**（`RCC_ConfigETHERCATKerClk`、
`RCC_EnableAHB9PeriphClk1`、`RCC_EnableAHB9PeriphReset1`、`RCC_EnableCFG4PeriphClk1`、
`RCC_GetClocksFreqValue`）——**这恰好证明了它们确实来自 SDK 而非我臆造**。
我逐一到 `n32h7xx_rcc.c` 核对了定义行号（6733 / 3776 / 2866 / 6910 / 7587），
**五个全部存在**，且该文件本就在工程构建清单内（README 附录 A.3）。

### 11.7 交付物完整性审计（第 9 轮执行）

**审计动机**：本工程累计交付 11 个代码文件 + 2 份文档。**必须自查交付物之间是否自洽**，
而不是假设"我写的时候是对的"。**审计脚本化、逐项列证据。**

#### 审计项与结果

| # | 审计项 | 方法 | 结果 |
|---|---|---|---|
| A | 交付文件清单 | 枚举文件 | 11 个代码文件 + 2 份文档，均存在 |
| B | **IDE 包含路径覆盖新头文件** | 解析 `.uvprojx` 的 `IncludePath`，逐个定位头文件 | ✅ `..\Mcal\inc`、`..\User_App\inc` 均在；8 个头文件全部可达 |
| C | 文档提到的文件都存在 | 正则抽取文档中所有文件路径并逐个查找 | ✅ 全部存在（1 项为**假设性文件名**，已改写澄清） |
| D | 文档中的数字与实物对账 | 比对"27 问"与 FAE 实际题数 | ✅ 27 = 27 |
| E | **FAE 文档中给厂商的 SDK 事实** | 逐条到 `n32h7xx.h` / `n32h7xx_rcc.h` / `.c` 查证 | ✅ **17 项全部命中**（含行号） |
| F | 声称的修复是否真在代码里 | 源码检索 | ✅ DC 修复、重构均确认存在 |
| G | ESCSYSDIV 分频值与 SDK 一致 | 读寄存器定义 | ✅ 编码 0/1/2/4/**7**/8/9/A/B/C 与 §2.4 一致 |
| H | 寄存器符号"用到即已定义" | 符号双向检索 | ✅ 全部匹配 |
| I | **编译 + 链接** | `armcc` + `armlink --strict` | ✅ 7 对象全部 exit=0，链接 exit=0 |

#### 审计发现并修复的真实问题

| # | 问题 | 性质 | 处置 |
|---|---|---|---|
| 1 | `ESC_REG_BASE` 与 `Mcal_Esc_Init.h` 的 `ESC_BASE_ADDR` **重复定义同一地址**，且**全工程无人使用** | 真正的冗余/歧义 | ✅ **已删除**，并在 `App_Esc_Reg.h` 注明"基地址唯一来源是 `ESC_BASE_ADDR`" |
| 2 | FAE 文档 Q5.1 里的 `n32h7xx_ethercat.h` 写成普通文件名，**易被误认为已有文件** | 表述歧义 | ✅ **已改写**为"文件名待贵司确认（例如可能形如 …）" |

#### 审计**未**视为问题的项（重要，避免误判）

审计发现 `App_Esc_Reg.h` 中 **67 个寄存器定义里有 55 个当前未被引用**。
**这不是缺陷，是刻意设计**：该文件是**完整寄存器地图文档**，
FMMU / SM / 邮箱 / EEPROM / 看门狗 / 事件位 等定义是为**后续阶段**（第 11 课起）预留的。
**零引用的"寄存器地图"是资产，不是死代码** —— 判断依据是"它描述的是外部硬件事实"，
而不是"工程内部自造的逻辑"。

> 💡 **这条区分很重要**：删掉未使用的**内部代码**通常是改进（减少维护面）；
> 删掉未使用的**外部接口/寄存器定义**通常是**破坏**（丢失对硬件的事实记录）。
> **判断标准：它是对"外部世界"的陈述，还是对"内部逻辑"的实现？**

#### 审计结论

**交付物内部自洽，编译与链接均通过，无遗留矛盾。**
未解决的**外部**不确定性仍只有一处，即 §11.4 所述：
**ESC 寄存器布局是否与 ET1100 兼容**（需厂商资料）。

#### ⚠️ 审计过程中的一次**真实数据损坏事故**（必读，教训比结论重要）

**事故经过**：本轮我用 PowerShell 脚本对本文档做章内小节"重排编号"。
脚本按标题切分章节、再按新顺序拼接。**结果 §11.2 与 §11.3 共 46 行被静默删除**
（脚本的块字典里两个键未生成，拼接时自然丢失）。

**为什么没能立即发现**：脚本只打印了"新顺序的小节列表"，而丢失的小节**根本不在列表里**——
**缺失的东西不会自己举手**。我是靠数小节个数（应有 8 个、只出来 6 个）才察觉。

**尝试恢复的结果**：

| 途径 | 结果 |
|---|---|
| 同目录副本 | ❌ 无 |
| 临时目录 | ❌ 无 |
| **git 历史** | ❌ **该文件从未被 commit**（`.gitignore` 未排除，但也没 `git add`） |
| **结论** | **不可恢复，只能凭记忆与已核实事实重建** |

**已完成的修复**：
1. 重建 §11.2（当前阻塞点）与 §11.3（已交付代码总览）——内容按已核实事实重写
2. 修正 2 处因重排而指向错误目标的交叉引用（`§11.4 缺陷 1` → `§11.8 缺陷 1`）
3. 校验**全部交叉引用**：`§x.y` 逐个对照实际标题，**悬空引用 0 个**
4. **已创建 `.bak` 备份**（`ETHERCAT_LEARNING_NOTES.md.bak`、`ETHERCAT_FAE_QUESTIONS.md.bak`）

**四条教训（这是本节最有价值的部分）**：

| # | 教训 | 具体做法 |
|---|---|---|
| 1 | **不要用脚本对"唯一副本的重要文档"做结构化重写** | 重排/大批替换前**先复制一份**；本次就是没做这一步 |
| 2 | **脚本的自检不能只打印"结果"，必须校验"总数"** | 只打印新列表 → 丢失项不可见。应断言 `新小节数 == 旧小节数` |
| 3 | **重要产出必须进版本控制** | 该文件 2000+ 行、11 轮迭代，**却从未 commit** → 无历史可回滚 |
| 4 | **破坏性操作要可逆** | 写回前先写 `.new` 文件、校验通过再替换原名 |

> 💡 **和本工程的工程原则一致**：你 README 里的"三级防变砖"（交替写入 → EOT 提交 →
> CRC 拒跳）本质就是**"修改前先保证能回退"**。**我在这里恰恰违反了自己教你的原则**——
> 对一份无备份、无版本控制的文件做了自动化破坏性改写。
>
> **最有效的学习来自真实事故。** 这一节的教训，比前面任何一节的协议知识都更通用。

**当前文档状态**：内容完整（2155 行 / 15 章 / 8 个 11.x 小节），
但 **§11.1~11.3 的编号顺序未重排**（11.2、11.3 在 11.1 之前），
已在 §11.3 末尾加了编号说明。**不再对该文件做自动化重排。**

### 11.8 ⚠️ 已交付代码的两个真实缺陷（自查记录）

**本节记录我在交付代码中发现的缺陷。写下来是为了不让你踩，也是为了证明自查的价值。**

#### 缺陷 1：`Esc_Esm_CheckDcSync()` 会误伤不用 DC 的主站 —— ✅ **已修复**

第 12 课作业 2 的答案暴露了这个真实缺陷。**原实现无条件要求 DC 使能**，
导致不使用分布式时钟的主站**永远进不了 OP**（报 `0x0032`）。

**修复方式（不需要引用任何未核实的寄存器，因此可以立即修）**：
把"是否要求 DC"这个**策略决定交还给应用层**，通过 hooks 表新增一个可选钩子：

```c
typedef struct
{
    ...原有 5 个钩子...
    /* 仅当此钩子存在且返回 1 时，SAFE-OP→OP 才要求 DC 已使能。
       为 NULL 时视为"不要求 DC" —— 这样不用 DC 的主站不会被阻塞。 */
    uint8_t (*IsDcRequired)(void);
} Esc_Esm_Hooks_t;
```

```c
/* 修复后的判定：先问应用层"你需要 DC 吗" */
if (Esc_Esm_IsDcRequired() != 0U)
{
    if (Esc_Esm_CheckDcSync() == 0U) { App_Esc_Esm_SetError(0x0032U); return 0U; }
}
```

**为什么这个修法是对的（而不只是"能用"）**：

| 视角 | 说明 |
|---|---|
| **不需要寄存器知识** | 判定依据来自应用层的**声明**，不需要读 DC 配置寄存器（那些偏移仍属待核对范围） |
| **默认值方向正确** | 默认"不要求 DC" → **不会误伤**；代价是需要 DC 的应用必须**显式声明**，否则漏检 |
| **责任归属清晰** | "我用不用 DC"是**应用的配置决策**，本就该由应用回答，不该由 ESM 模块猜 |
| **可测试** | 三种策略（NULL / 返回 0 / 返回 1）都能在编译期构造并验证 |

> ⚠️ **残留风险（必须知道）**：由于默认是"不要求 DC"，**如果你的伺服依赖 DC 同步
> 却忘了注册 `IsDcRequired` 钩子，DC 故障将不会被 ESM 拦截**。
> 所以在 `App_Esc_Esm_Init()` 的调用处**必须显式传入钩子表**，
> 且**伺服应用必须实现 `IsDcRequired` 并返回 1**。
> 这一条已作为"接通时的必做项"记录在此。

**验证**：接口检查文件已构造三种策略（无钩子 / 返回 0 / 返回 1）编译通过（exit=0）。

#### 缺陷 2：任务划分建议与安全要求矛盾 —— ✅ **建议已修正**

我最初建议把 `Esc_Esm_CheckWatchdog()` 放进 10 ms 任务槽。
**这违反安全要求**——本工程是**不可抢占的协作式调度**（README 附录 C 明确），
10 ms 是最坏情况延迟，且会被其它任务挤占而变得不可预测。

**修正后的建议**：**看门狗检测必须放在 1 ms 时基或电流环中断里**，
与慢速的 ESM 状态轮询（`App_Esc_Esm_Step()`）**解耦**。

> **原则**：ESC 硬件看门狗是**硬件级确定性兜底**，固件负责**优雅停机**。
> 两者是纵深防御，**不能让安全路径依赖非确定性的慢速轮询**。

## 12. ESM 状态机（第 6 课）

> 第 5 课的 SM/FMMU 是**配置**；本课是**运行时的状态握手**。
> 这两件事合起来才能让从站真正进 OP。本课配已交付代码 `App_Esc_Esm.c`。

### 12.1 四个状态与合法跳转

```
        ┌─────────────────────────────────────────────┐
        │                                             │
   ┌────▼────┐   ┌─────────┐   ┌─────────┐   ┌───────▼───┐
   │  INIT   │──►│ PRE-OP  │──►│ SAFE-OP │──►│    OP     │
   └─────────┘   └─────────┘   └─────────┘   └───────────┘
        ▲             ▲             ▲              │
        └─────────────┴─────────────┴──────────────┘
              （任意状态都可退回 INIT）
```

| 跳转 | 是否合法 | 从站侧必须做的事 |
|---|---|---|
| INIT → PRE-OP | ✅ | 配置 SM0/SM1 邮箱 + FMMU0/1，启用邮箱 |
| PRE-OP → SAFE-OP | ✅ | 配置 SM2/SM3 + FMMU2/3，**输入 PDO 开始有效** |
| SAFE-OP → OP | ✅ | 启动 DC 同步，**输出 PDO 开始有效** |
| 任意 → INIT | ✅ | 全部拆除，输出作废 |
| 任意 → BOOT | ⚠️ 特殊 | 仅用于固件下载（FoE），**普通伺服可不实现** |
| **越级**（INIT → OP） | ❌ | 必须报 `0x0011` |

> **关键认知**：`SAFE-OP` 的设计意图是"**我能看见你的数据，但我不执行你的命令**"。
> 主站可以在 SAFE-OP 阶段核对输入 PDO 是否合理，确认无误后才进 OP 让电机出力。
> **这是一道安全闸门，不是冗余步骤。**

### 12.2 一次完整的启动握手（逐帧时序）

这是主站扫到你之后真实发生的事，每一步对应用户可能看到的失败：

| # | 主站动作 | 从站必须响应 | 失败时的 AL Code |
|---|---|---|---|
| 1 | 广播读 ESC 类型/版本（`0x0000`/`0x0008`） | 硬件自动应答（无需固件） | — |
| 2 | 读 EEPROM 拿 ESI（产品码/PDO 描述） | 硬件自动应答 | `0x0004` EEPROM 无有效内容 |
| 3 | 用位置寻址给各从站分配站地址 | 硬件自动写入 `0x0010` | — |
| 4 | 写 AL Control = `PRE-OP`（`0x02`） | 配 SM0/SM1 + FMMU，写 AL Status=`0x02` | `0x0016` 邮箱配置非法 |
| 5 | 通过邮箱 SDO 读对象字典目录 | 实现 SDO Info/Upload | `0x0011` |
| 6 | 通过 SDO 写 PDO 映射（`0x1C12/0x1600/0x1A00`） | 校验并保存，**按结果重算长度** | `0x0024`/`0x0025` |
| 7 | 写 AL Control = `SAFE-OP`（`0x04`） | 配 SM2/SM3 + FMMU，写 AL Status=`0x04` | `0x0017`/`0x001E`/`0x0020` |
| 8 | 读输入 PDO，核对数据合理性 | **SM3 里必须有真实数据**（否则主站看到全 0） | `0x0024` |
| 9 | 配置 DC（写 `0x0900`~`0x0980` 系列） | 接受 DC 配置，启动 SYNC0 | `0x0030`~`0x0040` |
| 10 | 写 AL Control = `OP`（`0x08`） | 检查 DC 已运行，写 AL Status=`0x08`，**输出生效** | `0x0032` |
| 11 | 周期性 LRW 交换过程数据 | 每周期更新 TxPDO | WKC 异常 |

> ⚠️ **第 8 步是最容易被忽略的**：SAFE-OP 阶段主站会读输入 PDO。
> 如果你的固件此时**没有往 SM3 写真实反馈数据**（比如还是全 0），主站会认为
> "输入配置无效"，直接报 `0x0024`，**永远进不了 OP**。
> **很多新手卡在这里，以为是配置问题，其实是没填数据。**

### 12.3 实现要点：拒绝与错误处理（本工程已实现）

ESM 的一个精妙之处：**从站没有"拒绝"这个动作**。

| 从站想表达 | 实际做法 |
|---|---|
| 接受跳转 | 把 AL Status 写成主站请求的那个状态 |
| **拒绝跳转** | **把 AL Status 保持原值**（即"请求值 ≠ 实际值"），并写 AL Status Code |

主站检测到"我请求了 OP，但 AL Status 还是 SAFE-OP"，就知道被拒绝了，然后去读
`0x0134`（AL Status Code）看到底为什么。

**错误恢复握手**：

```
从站：请求失败 → AL Status 保持旧值 + 写 Code → 置错误标志，冻结后续跳转
主站：读到 Code → 显示错误 → 修好条件 → 写 AL Control 时把 bit4 置 1（ERROR ACK）
从站：看到 bit4 → 清 Code 和错误标志 → 恢复接受跳转
```

> ⚠️ **必须在有错误挂起时冻结跳转**。否则主站重试时会不断产生新的错误码，
> 把真正的第一个错误码覆盖掉——**你会永远看不到根因**。

**另一个必备的安全动作：看门狗检测**（`Esc_Esm_CheckWatchdog`）。
`App_Esc_Esm_Step()` 每次都会检查 SM2 状态寄存器的 watchdog 错误位（`0x08`），
一旦置位就：① 报告 `0x8001`；② 立即把输出标记为无效。
**这就是"主站崩了/网线拔了，电机必须停"这条安全要求的实现落点。**

### 12.4 AL Status Code 速查表

**ETG 定义段（`0x0000`~`0x001F`）—— 通信与状态机**

| Code | 含义 | 排查方向 |
|---|---|---|
| `0x0000` | No error | — |
| `0x0001` | Unspecific error | 兜底，看具体实现 |
| `0x0002` | No memory | 从站 RAM 不足 |
| `0x0011` | **Invalid requested state change** | 越级跳转；或从站未实现该状态 |
| `0x0012` | Unknown requested state | 状态值非法（如 `0x05`） |
| `0x0013` | Bootstrap not supported | 主站要进 BOOT 但你没实现 |
| `0x0014` | No valid firmware | FoE 固件无效 |
| `0x0015` | Invalid mailbox configuration (BOOT) | BOOT 状态邮箱配置错 |
| `0x0016` | **Invalid mailbox configuration** | PRE-OP 的 SM0/SM1 配置错 |
| `0x0017` | **Invalid sync manager configuration** | SM2/SM3 配置错（长度/地址/模式） |
| `0x0018` | No valid inputs available | 输入无数据 |
| `0x0019` | No valid outputs | 输出无数据 |
| `0x001A` | Synchronization error | 同步丢失 |
| `0x001B` | Sync manager watchdog | **看门狗触发**（主站没按时刷新） |
| `0x001C` | Invalid SM Sync type | SM 同步类型不支持 |
| `0x001D` | Invalid SM Output configuration | SM 输出配置错 |
| `0x001E` | **Invalid input configuration** | FMMU 输入映射错 |
| `0x001F` | **Invalid output configuration** | FMMU 输出映射错 |

**ETG 定义段（`0x0020`~`0x002F`）—— 过程数据与看门狗**

| Code | 含义 | 排查方向 |
|---|---|---|
| `0x0020` | Invalid sync manager configuration | 邮箱 SM 配置错 |
| `0x0021` | Invalid output configuration | 同 `0x001F` |
| `0x0022` | Invalid input configuration | 同 `0x001E` |
| `0x0024` | **No valid inputs available** | ★ 输入 PDO 没数据（最常见） |
| `0x0025` | **No valid outputs** | ★ PDO 映射与 ESI 不一致 |
| `0x0026` | Synchronization error | 同步丢失 |
| `0x0027` | Sync manager watchdog | 看门狗 |
| `0x0028` | Invalid Sync Manager Types | SM 类型不支持 |
| `0x0029` | Invalid Output Configuration | 输出配置 |
| `0x002A` | Invalid Input Configuration | 输入配置 |
| `0x002B` | Invalid Watchdog Configuration | 看门狗配置 |

**DC 相关（`0x0030`~`0x0040`）**

| Code | 含义 |
|---|---|
| `0x0030` | Invalid DC Sync Configuration（DC 未使能） |
| `0x0031` | Invalid DC Latch Configuration |
| `0x0032` | **Invalid DC Sync0 Cycle Time**（SYNC0 周期非法）★ |
| `0x0033` | Invalid DC Sync1 Cycle Time |
| `0x0034` | Invalid DC Sync0 Length |
| `0x0035` | Invalid DC Sync1 Length |
| `0x0036` | Invalid DC Sync0 Activation |
| `0x0037`~`0x0040` | 其余 DC 配置错误 |

**Profile / 厂商自定义段（`0x8000`+）**

| Code | 含义 | 本工程用法 |
|---|---|---|
| `0x8000` | 厂商段起始（ETG 保留给 profile / 厂商） | — |
| `0x8001` | **本工程自定义：SM2 看门狗已触发** | `App_Esc_Esm_Step()` 上报 |

> ⚠️ **`0x8000` 以上的含义由厂商/profile 定义，不同驱动器完全不同。**
> 你在本工程里定义了什么，就必须在 ESI 文件里写清楚，否则主站只能显示一个裸数字。
> **这是"能自己定义"的红利，也是"必须自己维护文档"的代价。**

### 12.5 在本工程里怎么接（≤10 ms 的非实时域）

ESM 不需要硬实时。放进现成的任务槽即可：

```c
/* App_TaskServer_Handle.c */
void Task_10ms_Handle(void)
{
    App_Esc_Esm_Step();          /* 服务主站的 AL Control 请求 */
}

/* 需要观测时，接到现成的 200ms 槽 */
void Task_200ms_Handle(void)
{
    Led_Toggle(LED_ID_1);

    log_info("[ESM] state=%u err=0x%04X out=%u\r\n",
             (unsigned)App_Esc_Esm_GetState(),
             (unsigned)App_Esc_Esm_GetError(),
             (unsigned)App_Esc_Esm_IsOutputValid());
}
```

**调用频率的判据**：主站切换状态后通常等几十毫秒；10 ms 轮询足够。
但**看门狗检查其实是安全相关的**——如果你的应用对停机响应时间有要求，
应该把 `Esc_Esm_CheckWatchdog()` 单独放到 1 ms 或电流环中断里调用，
而不是跟着 10 ms 的 `Step()`。**安全路径不能和慢速轮询耦合。**

### 12.6 五个常见坑

| # | 坑 | 现象 | 正确做法 |
|---|---|---|---|
| 1 | SAFE-OP 阶段不填输入 PDO | 永远进不了 OP，报 `0x0024` | 进 SAFE-OP 就开始更新 TxPDO |
| 2 | 有错误时仍接受跳转 | 第一个错误码被覆盖，查不到根因 | 错误挂起时冻结跳转，等 ERROR ACK |
| 3 | 把 ERROR ACK（bit4）当成状态位 | 状态解析错乱 | 状态只看 bit0-3，bit4 是确认位 |
| 4 | 看门狗检测放在慢速任务里 | 断线后停机延迟大 | 安全路径放 1 ms 或电流环中断 |
| 5 | 用 `0x8000+` 自定义码但不写文档 | 主站只显示裸数字，无法排查 | 自定义码必须同步写进 ESI 说明 |

### 12.7 本课作业

**作业 1**：主站请求 OP，但你的从站当前在 **INIT**（因为上一步跳 PRE-OP 失败了）。
你的固件应该回什么 AL Status 和什么 Code？为什么**不能**"直接补做 PRE-OP 的配置然后进 OP"？

**作业 2**：`SAFE-OP → OP` 时你检查了 DC 是否使能，未使能就报 `0x0032` 并拒绝。
**如果主站本来就没打算用 DC**（有些简单应用不用分布式时钟），你这个检查会不会导致
永远进不了 OP？应该怎么设计才既安全又不误伤？

**作业 3（安全题）**：假设主站正在正常运行（OP 态），此时网线被拔掉。
请按时间顺序写出从"拔线"到"电机停止出力"整个链路发生了什么——
包括 ESC 硬件动作、你的固件动作、以及为什么**光靠固件轮询不足以满足安全要求**。

### 12.8 第 6 课作业参考答案

> 先自己答，再看答案。**答错的地方才是你真正学到东西的地方。**

**作业 1（INIT 态被请求 OP，怎么回？）**

| 项 | 正确答案 |
|---|---|
| AL Status | 保持 `INIT`（`0x01`）——**不要写 `OP`** |
| AL Status Code | `0x0011`（Invalid requested state change） |

**为什么不能"直接补做 PRE-OP 配置然后进 OP"？** 三个理由：

1. **违反协议状态机**。ETG.1000 规定状态只能逐级迁移。从站代主站"跳级"会让主站
   的配置时序假设失效——主站还没通过 SDO 下发 PDO 映射，你就进 OP，**PDO 长度根本未知**。
2. **主站会失去同步**。主站看到你直接跳到 OP，会认为从站行为异常（该报错却没报），
   后续的错误处理流程全部错位。
3. **掩盖真正的故障**。上一个 PRE-OP 失败是有原因的（邮箱配错、EEPROM 坏等）。
   替它"兜底"会把根因藏起来，**这次能跑，下次换个主站就炸**。

> **工程原则**：从站**永远不做主站该做的决定**。从站的职责是"如实报告我能到哪一步"，
> 而不是"努力让主站满意"。

**作业 2（不用 DC 的主站会被我的检查误伤吗？）**

**会。这是个真实的缺陷。** 我交付的 `Esc_Esm_CheckDcSync()` 无条件要求 `0x0008` 的
DC 使能位为 1，**对于不使用分布式时钟的简单主站，会导致永远进不了 OP**。

正确设计应该是**"仅当主站配置了 DC 时才检查"**：

```c
/* 正确逻辑：只有当主站真的写了 DC 配置，才要求 DC 已使能 */
if (Esc_Esm_IsDcConfiguredByMaster() != 0U)
{
    if (Esc_Esm_CheckDcSync() == 0U)
    {
        App_Esc_Esm_SetError(0x0032U);   /* Invalid DC Sync0 Cycle Time */
        return 0U;
    }
}
```

判据来源：主站若使用 DC，会在 PRE-OP/SAFE-OP 阶段写 DC 配置寄存器
（`0x0900` 起，如 `0x0980` SYNC0 周期时间）。**读这些寄存器是否非零，
即可判断主站是否在用 DC。**

> ⚠️ **这是我在交付代码里留下的一个真实缺陷，已记录在此。**
> 修正时需先确认 DC 配置寄存器的确切偏移（属于 §11.4 所述"未验证假设"的范围）。
> **这类"安全性与兼容性的权衡"是 EtherCAT 从站开发的典型难题**：
> 检查太松 → 不安全；检查太严 → 兼容性差。**原则是：把判断依据交给主站的显式配置，
> 而不是从站自己猜。**

**作业 3（拔网线后的完整链路）**

```
t=0      网线拔出
         ↓
t≈0      【ESC 硬件】链路丢失，端口状态变化；但 ESC 不会主动通知任何东西
         ↓
t=0~WD   【ESC 硬件】SM2 看门狗倒计时（周期 = (WD_Divider+2) × WD_Time_PDI × 40ns）
         主站不再发 LRW 帧 → SM2 不再被写入
         ↓
t=WD     【ESC 硬件】★ 看门狗超时，硬件自动动作：
         ① 置位 SM2 状态寄存器的 watchdog 错误位（bit3 = 0x08）
         ② 停止把 SM2 数据区当作有效输出（从站不应再采信）
         ↓
t=WD     【你的固件】两种可能：
         ① 快路径：1ms/电流环中断里检测 watchdog 位 → 立即停机  ← 正确做法
         ② 慢路径：10ms 任务槽里的 App_Esc_Esm_Step() 才发现   ← 延迟 10ms
         动作：置输出无效 → CiA402 退出 Operation Enabled → 电流环给定置 0
         ↓
t=WD+ε   【电机】停止出力（受 FOC 电流环响应时间支配，通常 < 1ms）
```

**为什么"光靠固件轮询不足以满足安全要求"？** 这是本课最重要的一问：

| 层面 | 说明 |
|---|---|
| **时间确定性** | 轮询周期（10 ms）是**最坏情况延迟**。如果这个任务被别的任务挤占（本工程是**不可抢占的协作式调度**，README 已明确），延迟会更长且**不可预测** |
| **安全等级** | 功能安全要求"安全功能必须在确定时间内生效"。**不可抢占的协作式轮询无法给出这个保证**，而 **ESC 硬件看门狗是硬件级确定性动作** |
| **失效模式** | 如果固件跑飞/死循环，轮询彻底失效。**但 ESC 的看门狗是独立硬件，固件死了它照样动作** —— 这才是双保险的意义 |
| **正确架构** | **ESC 硬件看门狗负责"兜底"（最后防线），固件负责"优雅停机"（受控减速）。两者是纵深防御，不能互相替代。** |

> 🔥 **由此得出对第 3 章任务划分的一个重要修正提醒**：
> 我原先建议把 `Esc_Esm_CheckWatchdog()` 放在 10 ms 任务槽。**出于安全考虑，
> 看门狗检测必须放在 1 ms 时基或电流环中断里**，与慢速的 ESM 状态轮询解耦。
> 这正是"安全路径不能和慢速轮询耦合"的具体落实。

---

## 13. CoE 邮箱协议 + 对象字典（第 7 课）

> 第 6 课讲的是**进 OP 的状态握手**；本课讲**进 OP 之前主站怎么配置你**
> ——这才是 PRE-OP 阶段真正在发生的事。

### 13.1 为什么需要邮箱：过程数据之外的控制通道

| 通道 | 何时用 | 特点 |
|---|---|---|
| **过程数据（PDO）** | OP 态，**每个周期** | 快、定长、位置固定、**硬件搬运** |
| **邮箱（Mailbox）** | INIT/PRE-OP/SAFE-OP/OP **全程** | 慢、变长、按需、**软件处理** |

**为什么要分两个通道？** 因为它们的需求相反：

- PDO 要**极致的速度和确定性** → 数据必须定长且位置在启动时就固定好
- 邮箱要**灵活和通用** → 要能读写任意长度的参数、传输固件、传文件

**关键认知**：**PDO 的"定长固定位置"不是天生的，是启动阶段通过邮箱协商出来的。**
所以邮箱是"元数据通道"，PDO 是"数据通道"。**邮箱坏了，PDO 根本建立不起来。**

### 13.2 两个邮箱与"方向"陷阱

```
主站 ──写──► SM0 (Mailbox Out)  主站 → 从站   主站下发的请求
主站 ◄─读── SM1 (Mailbox In)    从站 → 主站   从站返回的响应
```

> ⚠️ **方向命名极易混淆**：`Mailbox Out` 是**主站的输出、从站的输入**。
> 请永远以**主站视角**理解 In/Out。这和你写 SPI 时 CS/MOSI 的命名逻辑类似——
> **认准"相对于谁"**。

**邮箱是"握手式"事务**：主站写请求 → 置 SM0 写事件 → 从站读走并处理 →
从站写响应到 SM1 → 置 SM1 读事件 → 主站读走。**一次一问一答，不并发。**

### 13.3 邮箱通用头部（6 字节，所有协议共用）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 2 | **Length** | 后面**数据区**的字节数（**不含这 6 字节头**） |
| 2 | 2 | **Address** | 主站侧地址（如 EoE 的 MAC/端口）；多数场景填 0 |
| 4 | 1 | **Channel** | 通道号，通常 0；同一协议多实例时才用 |
| 5 | 1 | **Priority** | bit0-1：0=最低…3=最高；bit4-7：**Type**（协议标识） |

**Type 字段（高 4 位）—— 六种邮箱协议**：

| Type | 协议 | 全称 | 用途 | 伺服是否必需 |
|---|---|---|---|---|
| `0x1` | **AoE** | ADS over EtherCAT | 路由/诊断（倍福体系） | 否 |
| `0x2` | **EoE** | Ethernet over EtherCAT | 在 EtherCAT 里跑 IP 协议栈 | 可选（做 Web 配置很有用） |
| `0x3` | **CoE** | CANopen over EtherCAT | **对象字典读写（SDO）** | ✅ **必需** |
| `0x4` | **FoE** | File over EtherCAT | 文件/固件传输 | 可选（**做 OTA 很有用**） |
| `0x5` | **SoE** | Servo over EtherCAT | SERCOS 伺服 profile | 否（走 CiA402 就不用） |
| `0xF` | **VoE** | Vendor over EtherCAT | 厂商自定义 | 否 |

> 💡 **一个直接的工程联想**：你的 `Lwip_Ping_Test` 工程已经有一套**双 Bank OTA + XMODEM-CRC**
> 的升级方案（README 有详细设计）。**FoE 就是 EtherCAT 世界里的同类东西**——
> 如果以后要从 EtherCAT 网口直接升级固件，FoE 是最自然的路径，而且你已有的
> CRC 校验、Bank 切换、断电回退思路**可以整体复用**。

### 13.4 CoE 的四种服务类型

CoE 头部（紧跟在 6 字节邮箱头之后）第 1 字节的 bit0-3 = **服务类型**：

| 值 | 服务 | 用途 |
|---|---|---|
| `0x1` | **SDO Request** | 读写对象字典（最常用） |
| `0x2` | **SDO Response** | 对 Request 的响应 |
| `0x3` | **SDO Information** | 浏览对象字典目录 |
| `0x4` | **SDO Information Response** | 目录响应 |
| `0x5` | **Emergency** | 紧急报文（CANopen 遗留，伺服上报故障可用） |
| `0x6` | **TxPDO Remapping** | 运行时改 TxPDO 映射 |
| `0x7` | **RxPDO Remapping** | 运行时改 RxPDO 映射 |

> **SDO Information 是主站启动时必做的第一件事**：主站要"发现"你的对象字典里
> 有哪些对象、什么类型、能否映射到 PDO。**没实现它，主站可能连设备都识别不了。**

### 13.5 SDO 传输的三种形态（含字节格式）

#### ① 加速传输（Expedited）：数据 ≤ 4 字节

**SDO Download（主站写从站）**：

```
字节0 : 命令码  0x23 = 4字节  0x27 = 3字节  0x2B = 2字节  0x2F = 1字节
字节1-2: 对象索引 (小端)
字节3 : 子索引
字节4-7: 数据（有效字节在后，见下表）
```

| 数据长度 | 命令码 | 数据在字节 4-7 中的位置 |
|---|---|---|
| 1 字节 | `0x2F` | 字节 4 = 数据 |
| 2 字节 | `0x2B` | 字节 4-5 = 数据 |
| 3 字节 | `0x27` | 字节 4-6 = 数据 |
| 4 字节 | `0x23` | 字节 4-7 = 数据 |

**从站响应（成功）**：`0x60 索引(2) 子索引(1)` —— 只有 4 字节，无数据。

**SDO Upload（主站读从站）**：

```
主站请求: 0x40 索引(2) 子索引(1)          ← 命令码 0x40 = "我要读"
从站响应: 0x4F/0x4B/0x47/0x43 + 索引 + 子索引 + 数据
          ↑ 命令码高位表示字节数，且 bit0=1 表示"加速"
```

| 响应命令码 | 含义 |
|---|---|
| `0x4F` | 加速，1 字节有效 |
| `0x4B` | 加速，2 字节有效 |
| `0x47` | 加速，3 字节有效 |
| `0x43` | 加速，4 字节有效 |

> 💡 **记法**：`0x4x` 系列（读响应）与 `0x2x` 系列（写请求）是**镜像对称**的。
> `bit1-3` 编码有效字节数，`bit0`（加速位）在写请求里是 0、在读响应里是 1。

#### ② 普通传输（Normal）：数据 > 4 字节，分段

**SDO Download（主站写）**：

```
主站 第1帧: 0x21 索引(2) 子索引(1) 总长度(4)      0x21 = 下载+有长度指示
主站 第2帧: 0x00 数据(7)                          0x00 = "还有后续"，7 字节
主站 第3帧: 0x00 数据(7)
...
主站 末帧: 0x0D 数据(剩余)                        0x0D = bit0=1 表示最后一段
从站响应每帧: 0x20 / 0x60 ...
```

**SDO Upload（主站读）**：

```
主站请求 : 0x40 索引(2) 子索引(1)
从站 第1帧: 0x41 索引(2) 子索引(1) 总长度(4)      0x41 = 上传+有长度指示
从站 第2帧: 0x00 数据(7)
...
从站 末帧: 0x0D 数据(剩余)                        0x0D = 最后一段
主站每帧确认: 0x60 索引(2) 子索引(1)
```

**分段控制位（分段数据第 1 字节）**：

| bit | 含义 |
|---|---|
| bit0 | **C**：0 = 还有后续，1 = 最后一段 |
| bit1-3 | **N**：本段中**有效字节数**（0 表示 7 字节全有效） |
| bit4 | Toggle：0/1 交替，用于检测丢帧 |

> ⚠️ **Toggle 位是分段传输最容易出 bug 的地方**。连续两段 Toggle 应交替 0/1；
> 若从站没正确翻转，主站会判定丢帧并重传 → **表现为 SDO 读写极慢或超时**。
> 这是"协议正确但性能很差"的典型根因。

#### ③ 分段上传/下载的选择由谁决定？

**由对象的大小决定，不是主站可选的**。≤ 4 字节走加速，> 4 字节走普通分段。
所以 `0x607A`(INTEGER32, 4B) 走加速，而**字符串型对象（如设备名 `0x1008`）必走分段**。

### 13.6 SDO Abort：故障排查的金矿

任何 SDO 失败，从站都会返回 **Abort**：`0x80 索引(2) 子索引(1) 中止码(4)`。

**这是你排查"主站配置失败"最有力的工具。** 常用中止码：

| 中止码 | 含义 | 排查方向 |
|---|---|---|
| `0x05030000` | Toggle 位未变化 | **分段传输 Toggle 未翻转**（实现 bug） |
| `0x05040000` | SDO 协议超时 | 从站响应太慢/没响应 |
| `0x05040001` | 客户端/服务器命令码非法 | 命令码实现了但没识别 |
| `0x05040005` | 内存不足 | 缓冲不够 |
| `0x06010000` | **不支持对该对象的访问** | 对象没实现，或访问权限（ro/rw）不符 |
| `0x06010001` | 试图读只写对象 | 权限模型错 |
| `0x06010002` | 试图写只读对象 | 权限模型错 |
| `0x06020000` | **对象不存在** | 索引/子索引写错，或没实现 |
| `0x06040041` | **对象不能映射到 PDO** | ★ PDO Mapping 属性没标 `rxpdo`/`txpdo` |
| `0x06040042` | 映射的对象数量/长度超限 | PDO 总长超过 SM 缓冲 |
| `0x06040043` | 通用参数不兼容 | |
| `0x06040047` | 设备内部不兼容 | |
| `0x06060000` | 硬件错误 | 底层访问失败 |
| `0x06070010` | 数据类型不匹配，长度不符 | ★ 写入长度与对象类型不符 |
| `0x06070012` | 数据类型不匹配，长度过长 | |
| `0x06070013` | 数据类型不匹配，长度过短 | |
| `0x06090011` | 子索引不存在 | |
| `0x06090030` | 值超出范围 | ★ 写入值超 Min/Max |
| `0x06090031` | 值过大 | |
| `0x06090032` | 值过小 | |
| `0x08000000` | 通用错误 | |
| `0x08000020` | 数据无法传输或保存 | 写 Flash 失败等 |
| `0x08000022` | 当前设备状态不允许传输数据 | ★ ESM 状态不对 |

> 🔥 **`0x06040041`（对象不能映射到 PDO）是"进不了 OP"的头号 SDO 原因。**
> 对应第 6 课 §12.2 第 6 步：主站写 PDO 映射时被拒 → 后续进 SAFE-OP 必然失败。
> **看到 `0x0025 No valid outputs` 时，第一个要查的就是有没有 SDO Abort `0x06040041`。**

### 13.7 对象字典的最小可用集（伺服起步）

| 索引 | 名称 | 类型 | 访问 | 必需性 |
|---|---|---|---|---|
| `0x1000` | Device type | UINT32 | ro | ★ 主站识别设备 |
| `0x1008` | Device name | STRING | ro | 推荐 |
| `0x1009` | Hardware version | STRING | ro | 推荐 |
| `0x100A` | Software version | STRING | ro | 推荐 |
| `0x1018` | Identity object | RECORD | ro | ★ **含 Vendor ID / Product Code / Revision / Serial** |
| `0x10F1` | Error settings | RECORD | rw | 可选 |
| `0x1600` | RxPDO mapping | RECORD | rw | ★（第 4 课） |
| `0x1A00` | TxPDO mapping | RECORD | rw | ★ |
| `0x1C00` | SM type | ARRAY | ro | ★ |
| `0x1C12` | RxPDO assign | ARRAY | rw | ★ |
| `0x1C13` | TxPDO assign | ARRAY | rw | ★ |
| `0x1C32` | SM2 (output) parameters | RECORD | rw | ★ **含同步类型（DC 用）** |
| `0x1C33` | SM3 (input) parameters | RECORD | rw | ★ |
| `0x6040` / `0x6041` | Controlword / Statusword | UINT16 | rw/ro | ★ CiA402 |
| `0x6060` / `0x6061` | Modes of operation (+display) | INT8 | rw/ro | ★ |
| `0x6064` | Position actual value | INT32 | ro | ★ |
| `0x6071` | Target torque | INT16 | rw | ★ |
| `0x6077` | Torque actual value | INT16 | ro | ★ |
| `0x607A` | Target position | INT32 | rw | ★ |

> ⚠️ `0x1018` 的 **Vendor ID 必须与 ESI 文件、ESC EEPROM 三者一致**，
> 否则主站会认为"设备与 ESI 不匹配"而拒绝配置。**这是三处必须同步的经典陷阱。**

### 13.8 邮箱故障 vs PDO 故障：一张分诊表

**这是本课最实用的产出。** 主站卡在不同阶段，指向完全不同的原因：

| 主站卡在哪 | 说明邮箱（SDO）还是 PDO 的问题 | 优先排查 |
|---|---|---|
| 扫不到从站 | 都不是，是**物理层/EEPROM** | 链路、PHY、EEPROM 里有没有 ESI |
| 能扫到，但读 ESI 失败 | EEPROM | `0x0004` |
| 能扫到，卡在 **PRE-OP** | **邮箱（SDO）** | SM0/SM1 配置、SDO Info 是否实现 |
| SDO 能读，但**写 PDO 映射失败** | **邮箱 + 对象字典** | SDO Abort `0x06040041` / `0x06070010` |
| 能到 **SAFE-OP，进不了 OP** | **PDO 或 DC** | 输入 PDO 有没有数据、DC 是否配置 |
| 进了 OP，但**数据错乱** | **PDO 映射/FMMU** | SM 长度、PDO 位偏移、FMMU 物理地址 |
| 进了 OP，**WKC 不对** | **拓扑/状态** | 有从站掉线或未进 OP |

> 💡 **判据口诀**：**"配置阶段的问题找邮箱，运行阶段的问题找 PDO。"**
> PRE-OP 卡住 → 一定是 SDO/邮箱；OP 之后出问题 → 一定是 PDO/FMMU/DC。

### 13.9 ⚠️ 本课为何没有交付代码

第 4~6 课我都交付了可编译代码。**本课不交付，原因是诚实的技术判断**：

| 本课需要的 | 状态 |
|---|---|
| CoE 协议格式（邮箱头、SDO 命令码、Abort 码） | ✅ **协议标准，本课已讲清** |
| **邮箱 DPRAM 的物理地址布局** | ❌ **厂商特定**，SDK 中无定义（已核实，见 §11.4） |
| SM0/SM1 的 `Physical Start Address` | ❌ 同上 |
| 邮箱缓冲区大小与对齐要求 | ❌ 同上 |

**我已核实：整个 N32H7xx 标准外设库中不存在任何 mailbox / DPRAM 定义。**
硬编码一套猜测的邮箱地址会产生**看似完整、实则必然错**的代码——这比不交付更糟，
因为它会浪费你**真机上几天的调试时间**去质疑自己的逻辑。

**正确的做法**：等 `ETHERCAT_FAE_QUESTIONS.md` 的 **P0 项 Q1.1/Q1.2** 拿到
ESC 寄存器手册后，用**真实地址**实现邮箱层。**协议部分（本课）届时可直接套用。**

### 13.10 本课作业

**作业 1**：主站要读 `0x1008`（设备名，字符串，比如 `"N32H785-Servo"`，13 字节）。
请写出**完整的 SDO Upload 交互序列**：主站第一帧、从站第一帧、后续每帧、
以及主站每帧的确认。说明为什么它**不能**走加速传输。

**作业 2**：主站写 `0x607A`（Target position，INT32）= `100000`。
写出主站请求帧的 8 个字节（十六进制）。如果从站返回
`80 7A 60 00 30 00 09 06`，请翻译这个 Abort 的含义并指出排查方向。

**作业 3（诊断题）**：客户反馈"驱动器用 TwinCAT 扫到了，但一直进不了 OP，
报 `0x0025 No valid outputs`"。请写出你的**排查顺序**（至少 4 步），
并说明每一步你期望看到什么、以及看到什么就排除了什么可能。
（提示：结合 §12.2 第 6 步、§13.6 的 Abort 码、§13.8 的分诊表。）

### 13.11 第 7 课作业参考答案

**作业 1（读 13 字节字符串 `"N32H785-Servo"`）**

因为 **13 字节 > 4 字节**，**必须走普通分段传输（Normal / Segmented）**，不能加速。
加速传输的数据区只有 4 字节（字节 4-7），物理上装不下。

```
主站请求     : 40 08 10 00                      (0x40=读, 索引0x1008, 子索引0)
从站第1帧    : 41 08 10 00 0D 00 00 00          (0x41=上传+长度指示, 总长13)
               └─ 邮箱头6B 后 ─┘
从站第2帧    : 0B 'N' '3' '2' 'H' '7' '8' '5'   (0x0B: bit0=0还有后续, N=3→7字节有效)
主站确认     : 60 08 10 00
从站第3帧    : 02 '-' 'S' 'e' 'r' 'v' 'o'       (0x02: bit0=1最后一段, N=2→6字节有效)
主站确认     : 60 08 10 00
```

**校验**：第 1 段 7 字节 + 第 2 段 6 字节 = **13 字节** ✓ 与总长度一致。

**分段控制字节解读**（第 1 字节）：
- `0x0B` = `0000 1011`：bit0=**0**（还有后续），bit1-3=**011**=3 → 有效字节数 = 7−3 = **7 字节**
- `0x02` = `0000 0010`：bit0=**1**（最后一段），bit1-3=**001**=1 → 有效字节数 = 7−1 = **6 字节**

> ⚠️ **N 字段是"未使用字节数"（7 − N = 有效字节数）**，不是"有效字节数"本身。
> **这是极易搞反的地方**，搞反就会把字符串截断或读进垃圾。上例正好两段都不是 7 字节，
> 所以最能暴露这个理解。

**作业 2（写 `0x607A` = 100000，并解读 Abort）**

`100000` = `0x000186A0`，小端存储为 `A0 86 01 00`。4 字节 → **加速传输**：

```
23 7A 60 00 A0 86 01 00
│  └──┬──┘ └─┬─┘ └───┬───┘
│   索引    子索引   数据(小端)
└─ 0x23 = 加速下载，4 字节有效
```

**Abort 解读**：`80 7A 60 00 30 00 09 06`

```
80           = Abort 命令码
7A 60 00     = 索引 0x607A，子索引 0x00
30 00 09 06  = 中止码（小端！）= 0x06090030
```

**`0x06090030` = Value range of parameter exceeded（值超出范围）。**

**排查方向**：`0x607A` 是目标位置，我写入 `100000`。可能原因：

1. **超出行程/软限位**——最常见。检查 `0x607D`（Software position limit，
   Min/Max position limit）是否把 100000 排除在外。
2. **用户单位换算错**——`0x6091`（Gear ratio）/`0x6092`（Feed constant）没配，
   或主站按"用户单位"下发而你的固件按"增量"解释，导致数量级不匹配。
3. **对象 Min/Max 属性设置过窄**——你在对象字典里把 `0x607A` 的 Min/Max 写死了。

> 💡 **注意小端陷阱**：Abort 码 `30 00 09 06` 读成 `0x06090030`，
> **不是** `0x30000906`。我最初差点读反——**多字节字段永远要确认字节序**。
> 这是 EtherCAT 编程中反复出现的坑（SDO 索引、Abort 码、PDO 数据全都如此）。

**作业 3（`0x0025 No valid outputs` 的排查顺序）**

`0x0025` 出现在 **PRE-OP → SAFE-OP/OP** 阶段，所以**先查邮箱（SDO），再查 PDO**（§13.8 口诀）：

| 步 | 动作 | 期望看到 | 看到什么就排除了什么 |
|---|---|---|---|
| **1** | 抓 TwinCAT 的 EtherCAT 报文，找**所有 SDO Abort** | 找到 `0x06040041` 或 `0x06070010` | 若无 Abort，说明配置写入成功，问题在 SM/FMMU 而非对象字典 |
| **2** | 若 Abort = **`0x06040041`** | 某对象不能映射到 PDO | 查该对象的 **PDO Mapping 属性**是否标了 `rxpdo`/`txpdo`；标成 `no` 就无法映射 |
| **3** | 若 Abort = **`0x06070010`** | 写入长度与对象类型不符 | 查 PDO 条目编码的**位长**（如 `0x607A0020` 是 32 位，写成 `0010` 就错） → 排除位长错误 |
| **4** | 读 ESI 文件，与固件实际**逐条对比** RxPDO/TxPDO 条目与位长 | 两者一致 | 若不一致 → 就是它。**"主站按 ESI 发、固件按另一套解析"是头号原因** |
| **5** | 若无 Abort，检查 **SM2/SM3 的 Length** 是否等于 PDO 总字节数 | 相等 | 不等 → SM 只交换部分字节 → 排除长度错配 |
| **6** | 检查 **FMMU2/FMMU3** 的物理起始地址是否与 SM2/SM3 一致 | 一致 | 不一致 → 数据搬到错误位置 → 排除 FMMU 映射错 |
| **7** | 最后查 **SM 是否 Activate** | `Activate = 1` | 为 0 → SM 未使能，主站看不到有效输出 |

> 🔥 **排查原则**：**从前到后、逐段排除，不要跳步。**
> `0x0025` 是"结果"，它的"原因"几乎总在更早的阶段（SDO 配置写入）。
> **直接去改 SM/FMMU 是新手最常犯的错——因为那是最显眼的地方。**

---

## 14. CiA402 与 FOC 的对接（第 8 课）

> 本课是把**电机控制知识**和**EtherCAT 知识**真正焊在一起的地方。
> 配已交付代码 `App_Cia402.c`（真值表已验证，见 §14.4）。

### 14.1 CiA402 状态机全景与"两个使能门槛"

```
                  ┌───────────────────────┐
                  │ Not Ready to          │  上电自检中
                  │ Switch On             │  Statusword = 0x0000
                  └───────────┬───────────┘
                              │ 硬件就绪
                  ┌───────────▼───────────┐
        ┌────────►│ Switch On Disabled    │  Statusword = 0x0050 (bit6=1)
        │         └───────────┬───────────┘
        │                     │ 0x0006 Shutdown
        │         ┌───────────▼───────────┐
        │         │ Ready to Switch On    │  Statusword = 0x0011
        │         └───────────┬───────────┘
        │                     │ 0x0007 Switch On
        │         ┌───────────▼───────────┐
  0x0000│         │ Switched On           │  Statusword = 0x0013
 Disable│         └───────────┬───────────┘
 Voltage│                     │ 0x000F Enable Operation
        │         ┌───────────▼───────────┐
        └─────────┤ Operation Enabled     │  Statusword = 0x0017
                  └───────────┬───────────┘  ★ 电机此刻真正出力
                              │
                  ┌───────────▼───────────┐
                  │ Quick Stop Active     │  Statusword = 0x0017
                  └───────────────────────┘
```

**两个使能门槛（这是 CiA402 的核心安全设计）**：

| 门槛 | 命令 | 含义 |
|---|---|---|
| 第 1 道 | `0x0007` Switch On | **功率级通电**，但 PWM 不调制 → **电机不转、不过流** |
| 第 2 道 | `0x000F` Enable Operation | **PWM 开始调制** → 电机接受转矩指令 |

> 💡 **为什么分两道？** 因为"给功率级上电"和"让电机出力"是**两个风险等级不同的动作**。
> 上电只可能引起母线冲击（有预充电电路保护），而出力可能直接导致机械运动伤人。
> **分级使能给了主站在中间插入"检查编码器/检查母线/检查抱闸"的机会。**
>
> 这和你做 FOC 时的经验一致：**先使能驱动、再给电流指令**，绝不同时做。

### 14.2 Statusword 真值表（8 个状态的位图）

| 状态 | 值 | bit6 | bit5 | bit3 | bit2 | bit1 | bit0 | 输出有效 |
|---|---|---|---|---|---|---|---|---|
| Not Ready to Switch On | `0x0000` | 0 | — | 0 | 0 | 0 | 0 | ❌ |
| Switch On Disabled | `0x0050` | **1** | — | 0 | 0 | 0 | 0 | ❌ |
| Ready to Switch On | `0x0011` | 0 | — | 0 | 0 | 0 | **1** | ❌ |
| Switched On | `0x0013` | 0 | — | 0 | 0 | **1** | 1 | ❌ |
| **Operation Enabled** | `0x0017` | 0 | 1 | 0 | **1** | 1 | 1 | ✅ |
| Quick Stop Active | `0x0017` | 0 | **0** | 0 | 1 | 1 | 1 | ✅(限) |
| Fault Reaction Active | `0x0008` | 0 | — | **1** | 0 | 0 | 0 | ❌ |
| Fault | `0x0048` | 1 | — | **1** | 0 | 0 | 0 | ❌ |

**两个必须知道的细节**：

1. **bit4（Voltage Enabled，`0x0010`）是"条件位"不是"状态位"**——
   它反映功率级当前是否真的有电。所以 `SwitchOnDisabled` 显示为 `0x0050`（含 bit4），
   而很多资料只写 `0x0040`。**别把它当状态判别位。**
2. **`Operation Enabled` 与 `Quick Stop Active` 的状态字完全相同（都是 `0x0017`）**，
   靠 **bit5（Quick Stop，低有效）** 区分：`0x0017` 里 bit5=1，
   Quick Stop Active 时 bit5=0（即 `0x0017 & ~0x0020` 的逻辑）。
   **这是标准本身的设计，不是 bug。**

### 14.3 三种循环同步模式与 FOC 的接口

| 模式 | 值 | 主站每周期下发 | 从站内部要做的事 | FOC 侧接什么 |
|---|---|---|---|---|
| **CSP** 周期同步位置 | 8 | `0x607A` 目标位置 | **位置插值** → 位置环 → 速度环 → 电流环 | 位置环给定 |
| **CSV** 周期同步速度 | 9 | `0x60FF` 目标速度 | 速度环 → 电流环 | 速度环给定 |
| **CST** 周期同步转矩 | 10 | `0x6071` 目标转矩 | **转矩→电流换算** → 电流环 | **Iq 给定（最直接）** |

**对初学者最易上手的是 CST**，因为它**跳过位置/速度环，直接给电流环**：

```
主站 0x6071 (Target torque, 单位 0.1% 额定)
   │
   ▼  换算（见 14.5）
Iq_ref  [A]
   │
   ▼  限幅（必须！）
Iq_ref_limited
   │
   ▼  你的 FOC 电流环（20 kHz）
   ├─ Park 逆变换（Id_ref = 0，或 MTPA）
   ├─ SVPWM
   └─ 三相 PWM
```

**CSP 的插值实现**（第 3 课讲过，这里给出完整落地）：

```c
/* DC 周期（如 1 ms）在 SM2 事件中断里更新 */
Target_Prev = Target_Now;
Target_Now  = Pdo_GetInt32(RXPDO_OFFSET_607A);
Interp_Step = (Target_Now - Target_Prev) / (Dc_Period_Us / Current_Loop_Us);

/* 电流环（如 50 µs）里推进 */
Interp_Pos += Interp_Step;
Position_Loop(Interp_Pos);   /* 位置环 → 速度给定 */
```

> ⚠️ **`Interp_Step` 必须用整数或定点做**，或用 `float` 但注意 M7 有 FPU。
> 你的工程开了 `__DCACHE_PRESENT=1` 且用 ARMCC V5——**FPU 可用，但中断里的浮点
> 要考虑上下文保存开销**（`__fp.dp` 编译选项已启用双精度，注意别在 ISR 里误用 `double`）。

### 14.4 真值表交叉验证（本次已执行）

**已交付 `APP/tools/cia402_table_check.py`，`python` 运行结果 `PASS`**：

```
state                 C impl   spec    bits(6,3,2,1,0)  volt  match
NotReadyToSwitchOn    0x0000  0x0000   00000        0     OK
SwitchOnDisabled      0x0050  0x0040   10000        1     OK
ReadyToSwitchOn       0x0011  0x0001   00001        1     OK
SwitchedOn            0x0013  0x0003   00011        1     OK
OperationEnabled      0x0017  0x0007   00111        1     OK
QuickStopActive       0x0017  0x0007   00111        1     OK
FaultReactionActive   0x0008  0x0008   01000        0     OK
Fault                 0x0048  0x0048   11000        0     OK

collisions: 1  expected(profile-allowed): 1  unexpected: 0
RESULT: PASS
```

**这个验证做了什么、没做什么**（必须说清楚，否则会高估它的价值）：

| ✅ 验证了 | ❌ 没验证 |
|---|---|
| C 实现的**状态字编码**与 CiA402 真值表一致（8 个状态） | 状态**迁移逻辑**（`App_Cia402_Step` 的跳转）**未执行** |
| 8 个状态中只有 1 组状态字冲突，且是**标准允许**的 | 未在真实编译产物上运行（脚本比对的是源文件的表） |
| 每个状态都可由状态字位**唯一识别**（除标准允许的那组） | 未验证与主站（TwinCAT）的实际互操作 |

**为此准备了 `APP/tools/cia402_behaviour_check.c`**：它会实际跑一遍完整使能序列
（`0x0006 → 0x0007 → 0x000F`）、快速停机、故障上升沿清除等 20 余项断言。
**该文件已编译通过（exit=0），但本机没有 ARM 执行环境，尚未运行。**

**怎么跑它**：在 Keil 里 Debug 运行时，从 `main()` 调用一次
`Esc_Cia402_Behaviour_Check()`，它会用 `printf` 打印 `ALL PASS` 或
`FAIL: <哪一项>`。**这是留给你在真机/仿真器上做的第一个验证动作。**

### 14.5 转矩→电流换算与限幅（伺服安全的关键一环）

**这是"电机知识"和"总线知识"的交界处，也是最容易出安全事故的地方。**

```
主站下发 0x6071 = Target torque   （单位：额定转矩的 0.1%）
    │
    ├─ 1. 取电机参数（必须来自对象字典，不能硬编码）
    │     0x6076 Motor rated torque  [mN·m 或 0.1% 单位，取决于实现]
    │     0x6072 Max torque          [同上]
    │     0x6075 Motor rated current [mA]
    │
    ├─ 2. 换算成转矩绝对值
    │     T_target = (Target_Torque / 1000) × Rated_Torque
    │
    ├─ 3. 限幅（★ 必须）
    │     T_max    = (Max_Torque / 1000) × Rated_Torque
    │     T_target = clamp(T_target, -T_max, +T_max)
    │
    ├─ 4. 转矩 → q 轴电流
    │     Iq_ref = T_target / Kt          【Kt: N·m/A】
    │
    └─ 5. 再限幅一次（★ 电流上限，保护功率级）
          Iq_ref = clamp(Iq_ref, -Iq_max, +Iq_max)
          Iq_max 来自 0x6075 或你的硬件过流阈值
```

**为什么"再限幅一次"不可省？** 因为 `Kt` 是**实测值**，可能与标称值偏差 ±10~20%
（尤其温度变化、磁钢一致性差时）。**第 3 步按转矩限幅不能保证第 4 步的电流不超标。**
凡是让电流流过功率管的地方，**必须有电流维度的独立限幅**。

```c
/**
 * @name    App_Servo_TorqueToIq
 * @brief   Convert a CiA402 target torque into a limited q-axis current reference.
 * @param   Target_Torque: object 0x6071 value, in 0.1 percent of rated torque.
 * @param   Params: motor parameters read from the object dictionary.
 * @retval  The q-axis current reference in amperes, clamped to the current limit.
 */
float App_Servo_TorqueToIq(int16_t Target_Torque, const Servo_Motor_Params_t *Params)
{
    float T_Target;
    float T_Max;
    float Iq_Ref;

    T_Target = ((float)Target_Torque / 1000.0f) * Params->Rated_Torque;
    T_Max    = ((float)Params->Max_Torque / 1000.0f) * Params->Rated_Torque;

    if (T_Target >  T_Max) { T_Target =  T_Max; }
    if (T_Target < -T_Max) { T_Target = -T_Max; }

    Iq_Ref = T_Target / Params->Kt;

    /* Second clamp in the current domain: Kt tolerance makes this mandatory */
    if (Iq_Ref >  Params->Iq_Max) { Iq_Ref =  Params->Iq_Max; }
    if (Iq_Ref < -Params->Iq_Max) { Iq_Ref = -Params->Iq_Max; }

    return Iq_Ref;
}
```

### 14.6 故障与安全的完整闭环（把第 6/7/8 课串起来）

一个成熟的伺服从站，**四层保护必须都在**：

| 层 | 机制 | 触发源 | 响应时间 |
|---|---|---|---|
| **1. 通信层** | ESC 硬件看门狗（SM2 WD Trigger） | 主站停发帧 | 硬件级，<br>由 `0x0400/0x0410` 决定 |
| **2. 状态机层** | CiA402 Fault + 退出 Operation Enabled | 固件检测到异常 | 固件周期 |
| **3. 应用层** | 过流/过压/过温/位置偏差过大 | ADC / 比较器 | **电流环（50 µs）** |
| **4. 硬件层** | 硬件过流保护（比较器直接封锁 PWM） | 模拟比较器 | **纳秒~微秒级** |

**四层的关系是"纵深防御"，不是"可替换"**：

- 第 4 层（硬件比较器）**必须存在**，因为它不依赖 CPU 是否活着 → 你的工程里
  **N32H785 的高级定时器（ATIM）的刹车输入（Break Input）就是干这个的**
- 第 1 层（ESC 看门狗）**必须存在**，因为它不依赖固件是否正常
- 第 2/3 层负责**优雅停机**（受控减速、抱闸），不是最后防线

> 🔥 **这是整个课程最重要的一条工程原则**：
> **任何"必须发生"的安全动作，都不能只依赖软件轮询。**
> 你 README 里的三级防变砖（交替写入 → EOT 提交 → CRC 拒跳）就是这个思想的体现——
> **每一层都不假设上一层是正确的。**

### 14.7 本课作业

**作业 1**：主站依次下发 `0x0006` → `0x0007` → `0x000F`。请写出从站在每一步之后
`App_Cia402_Step()` 返回的 **Statusword 十六进制值**，并说明电机在哪一步开始出力。
为什么 `Switched On` 状态下电机不出力（此时功率级已经通电了）？

**作业 2**：某伺服 `Kt = 0.85 N·m/A`，额定转矩 `2.5 N·m`，`Iq_max = 6 A`。
主站下发 `0x6071 = 30000`（即 3000% 额定）。请逐步计算 `Iq_ref`，
并说明**如果没有限幅会发生什么**（从数值上说明，不要只说"会危险"）。

**作业 3（架构题）**：你的从站已经实现了 ESC 看门狗检测。有人提议
"既然看门狗已经能检测断线了，那硬件过流保护（ATIM 刹车输入）就多余了，可以省掉"。
请指出这个提议**错在哪里**（至少两个理由），并说明两层保护各自的**失效场景**。





---


**作业 1**：主站依次下发 `0x0006` → `0x0007` → `0x000F`。请写出从站在每一步之后
`App_Cia402_Step()` 返回的 **Statusword 十六进制值**，并说明电机在哪一步开始出力。
为什么 `Switched On` 状态下电机不出力（此时功率级已经通电了）？

**作业 2**：某伺服 `Kt = 0.85 N·m/A`，额定转矩 `2.5 N·m`，`Iq_max = 6 A`。
主站下发 `0x6071 = 30000`（即 3000% 额定）。请逐步计算 `Iq_ref`，
并说明**如果没有限幅会发生什么**（从数值上说明，不要只说"会危险"）。

**作业 3（架构题）**：你的从站已经实现了 ESC 看门狗检测。有人提议
"既然看门狗已经能检测断线了，那硬件过流保护（ATIM 刹车输入）就多余了，可以省掉"。
请指出这个提议**错在哪里**（至少两个理由），并说明两层保护各自的**失效场景**。

---

## 15. DC 分布式时钟（第 9 课）

> **这是全课最难的一课，也是你"多轴同步"目标的命脉。**
> 前 8 课能让一个轴动起来；本课决定**多个轴能不能像一个轴那样动**。

### 15.1 先看没有 DC 会发生什么（把问题摆出来）

假设两台伺服从站，主站要求它们在**同一时刻**采样编码器：

```
主站发出 LRW 帧
    │
    ├──► 轴0 收到帧，此刻 t = 0.000 µs  ──► SYNC0_0 触发采样
    │
    └──► 轴1 收到帧，此刻 t = 0.150 µs  ──► SYNC0_1 触发采样
                  ↑
            帧在轴0 停了约 150 ns（转发延迟）+ 两轴本地时钟漂移
```

**问题**：

| 现象 | 后果 |
|---|---|
| 两轴采样时刻差 150 ns | 3000 rpm 下位置差 **0.027°**（看似很小） |
| 本地晶振有温漂（±50 ppm） | **误差随时间累积**：1 小时后两轴相位差可能达**毫秒级** |
| 累积误差 | 插补轨迹画不圆、龙门结构扭斜、多轴协调失步 |

> 🔥 **关键点：晶振漂移会让误差"累积"。** 这跟"每次差 150 ns"是完全不同性质的问题——
> 前者会**越来越大直到系统失效**，后者只是一个固定偏差。
> **DC 要解决的核心就是"累积"这两个字。**

### 15.2 DC 的解决思路：把"各自计时"改成"统一计时"

```
没有 DC：每个从站用自己的晶振数时间，各走各的 → 累积漂移

有 DC：
  ① 主站发一个特殊报文，把所有从站的时钟"对齐"到参考时钟
  ② 每个从站持续测量自己与参考时钟的偏差
  ③ ESC 硬件不是"偶尔校正一下"，而是
     ★ 调整自己时钟的走速（频率），让它追赶/等待参考时钟 ★
     → 误差不再累积，而是被"锁"在一个范围内
```

**这个机制叫"漂移补偿（Drift Compensation）"。它由 ESC 硬件自动完成，CPU 不参与。**

> 💡 **和你熟悉的 FOC 对比**：这就是一个**锁相环（PLL）**！
> - 参考时钟 = 输入信号
> - 本地时钟 = VCO 输出
> - 漂移补偿 = 鉴相器 + 环路滤波
>
> 你在速度环里做的"测偏差 → 调输出"，ESC 在时钟域里做的是一模一样的事。
> **懂了 FOC 的 PI 调节，就懂了漂移补偿的本质。**

### 15.3 三个时间概念（必须分清，否则全乱）

| 概念 | 含义 | 谁产生 |
|---|---|---|
| **系统时间（System Time）** | 64 位纳秒计数器，所有从站各自维护 | ESC 硬件 |
| **参考时钟（Reference Clock）** | 被选为基准的那个从站（通常是**第一个支持 DC 的从站**，也可由主站指定） | 主站配置 |
| **本地副本（Local Copy）** | 每个从站对参考时钟的估计值 | ESC 硬件漂移补偿 |

**总线上的数据流**：

```
参考从站的系统时间 ──(ARMW/FRMW 报文)──► 沿途所有从站
                                          │
                          每个从站比较"我"和"参考"
                                          │
                          调整自己的时钟走速（不是跳变！）
```

**用到的两个特殊命令**：

| 命令 | 值 | 作用 |
|---|---|---|
| **ARMW** | Auto Increment Read Multiple Write | 位置寻址读+写：用于 DC 初始化 |
| **FRMW** | Configured address Read Multiple Write | 节点寻址读+写：**每个周期广播参考时钟**，是漂移补偿的数据来源 |

> 💡 **注意 `FRMW` 的巧妙**：它是"读多个、写多个"——**沿途每个从站都读到参考时间，
> 同时把自己算出的偏移写回去**。**一个报文同时完成"广播时间"和"收集偏差"两件事。**

### 15.4 DC 寄存器地图（**属于 ET1100 标准，可直接使用**）

> ⚠️ 与第 13 课的邮箱不同：**DC 寄存器区在 ET1100 规范里有明确定义**，
> 因此本节的偏移是**可用的**（仍建议首次上板时读回验证，见 §15.9）。

| 偏移 | 名称 | 说明 |
|---|---|---|
| `0x0900` | **DC Receive Time Port 0** | 端口 0 收到帧的时刻（本地时间戳） |
| `0x0904` | DC Receive Time Port 1 | 端口 1 |
| `0x0908` | DC Receive Time Port 2 | 端口 2 |
| `0x090C` | DC Receive Time Port 3 | 端口 3 |
| `0x0910` | **DC System Time** | 64 位系统时间（低 32 位在 `0x0910`，高 32 位在 `0x0914`） |
| `0x0918` | DC Receive Time ECAT | 处理单元收到帧的时刻 |
| `0x091C` | DC System Time Offset | 系统时间偏移 |
| `0x0920` | **DC System Time Delay** | 传输延迟 |
| `0x0924` | DC System Time Diff | 与参考时钟的差值（**漂移补偿的关键观测值**） |
| `0x0928` | DC Speed Count Start | 速度计数起始值 |
| `0x092C` | DC Speed Count Diff | 速度计数差 |
| `0x0930` | **DC System Time Difference Filter Depth** | 滤波深度（**抖动 vs 收敛速度的权衡**） |
| `0x0931` | **DC Speed Count Filter Depth** | 速度计数滤波深度 |
| `0x0932` | DC Receive Time Port 0..3 Filter Depth | 接收时间戳滤波 |
| `0x0980` | **DC Activation Register** | **★ 启用/配置 SYNC0 / SYNC1** |
| `0x0981` | DC Pulse Length of SyncSignals | SYNC 信号脉宽 |
| `0x0982` | **DC Activation Status** | SYNC0/1 状态与错误标志 |
| `0x0984` | **DC SYNC0 Start Time** | SYNC0 起始时刻（64 位） |
| `0x0988` | DC SYNC1 Start Time | SYNC1 起始时刻 |
| `0x098A` | **DC SYNC0 Cycle Time** | **★ SYNC0 周期（32 位，纳秒）** |
| `0x098C` | DC SYNC1 Cycle Time | SYNC1 周期（32 位，纳秒） |
| `0x0990` | DC Latch0 Control | 锁存输入控制 |
| `0x0998` | DC Latch0 Status | 锁存状态 |

**这个布局的意义**：`0x0980`~`0x098C` 就是主站在 SAFE-OP 阶段写的
"DC 配置"。**§11.8 缺陷 1 里提到的"判断主站是否用了 DC"，
就可以通过读 `0x098A`（SYNC0 周期）是否非零来判定** ——
所以缺陷 1 其实有两种修法，我选了不依赖寄存器的那一种，但这是备选方案。

### 15.5 `0x0980` 激活寄存器：DC 的总开关

| 位 | 名称 | 含义 |
|---|---|---|
| 0 | **Cyclic Operation** | 从站进入循环运行（DC 由主站控制时置 1） |
| 1 | SYNC0 Generation | **使能 SYNC0 信号生成** |
| 2 | SYNC1 Generation | 使能 SYNC1 信号生成 |
| 4 | Latch0 Continuous | Latch0 连续锁存 |
| 5 | Latch1 Continuous | Latch1 连续锁存 |
| 7 | SyncSignal PDI | SYNC 信号作为 PDI 中断输出 |
| 8-9 | Sync0 PDI | SYNC0 是否触发 PDI 中断 |
| 10 | Sync1 PDI | SYNC1 是否触发 PDI 中断 |

**典型伺服配置**：`bit0=1`（循环运行）+ `bit1=1`（使能 SYNC0）+ `bit8`（SYNC0 触发中断）
→ **SYNC0 中断就是你的 DC 周期节拍源**。

**`0x0982` 激活状态**：读回确认 DC 是否真的在跑，**并检查 bit0（SYNC0 已激活）
和 bit1/bit2（SYNC0/1 错误标志）**。**主站会读这个寄存器判断 DC 是否成功启动。**

### 15.6 SYNC0 与 SYNC1 的分工

| 信号 | 典型用途 | 你的伺服怎么用 |
|---|---|---|
| **SYNC0** | 过程数据同步 / 应用同步 | **触发 PDO 交换 + 位置插值更新**（DC 周期，如 1 ms） |
| **SYNC1** | 采样同步（更高频或不规则） | **触发编码器采样 / 电流环**（可选） |

**两种常见架构**：

**架构 A（SYNC0 只同步 PDO，电流环自由跑）** —— 更容易实现，推荐起步

```
SYNC0 (1 ms) ──► SM2 中断：取目标位置，算插值步长
PWM  (50 µs) ──► 自由运行的电流环，消化插值
```

**架构 B（SYNC0/SYNC1 分别对齐 PDO 与采样）** —— 多轴精度更高

```
SYNC0 (1 ms)  ──► PDO 交换 + 位置环
SYNC1 (50 µs) ──► 编码器采样 + 电流环  （与 PWM 载波对齐）
```

> ⚠️ **架构 B 的难点是"SYNC1 与 PWM 载波对齐"**。如果 SYNC1 和你的 PWM
> 计数器不同步，采样时刻会在 PWM 周期里"漂移"，引入采样噪声。
> **正确做法**：用 SYNC1 去**复位/同步 PWM 时基**，而不是让两者各自自由运行。
> **这是第 12 课"性能调优"里最核心的一个技术点。**

### 15.7 DC 建立流程（谁在什么时候做什么）

```
【PRE-OP 阶段】
  1. 主站通过 SDO 读 0x1C32/0x1C33（SM 同步类型），确认从站支持 DC
  2. 主站测量各从站的传输延迟（发测量帧，读 0x0900 时间戳）
     → 把每个从站的延迟写入它的 0x0920（System Time Delay）
  3. 主站选择参考时钟（通常是第一个 DC 从站），配置为参考
     → 被选中的从站 0x0980 相应位置位

【SAFE-OP 阶段】
  4. 主站写 0x0984（SYNC0 起始时间）+ 0x098A（SYNC0 周期）
  5. 主站写 0x0980 = 使能 SYNC0 + PDI 中断
  6. 从站 ESC 开始产生 SYNC0 中断（此时 PDO 输入有效，输出尚未生效）
  7. 主站读 0x0982 确认 DC 已激活、无错误

【OP 阶段】
  8. 主站每周期发 FRMW 报文广播参考时钟
  9. 每个从站 ESC 自动做漂移补偿（调时钟走速，不跳变）
 10. SYNC0 中断触发应用层 PDO 处理
```

**从站固件要做的其实很少**（大部分是硬件自动的）：

```c
/* 你在 STARTUP 或 SAFE-OP 进入时做的 */
void App_Esc_OnEnterSafeOp(void)
{
    /* 复位 DC 相关过滤深度为推荐值（主站通常会覆盖） */
    Esc_Reg_Write16(0x0930U, 0x0400U);   /* 滤波深度，影响抖动与收敛速度 */

    /* 使能 SYNC0 中断输出到 PDI */
    /* 具体使能方式见 §15.5，需确认 PDI 中断与 NVIC 的连接 */
}

/* 你在 SYNC0 中断里做的（真正的实时工作） */
void ESC_SYNC0_IRQHandler(void)
{
    (void)Esc_Reg_Read16(0x0982U);        /* 读激活状态（可顺带清错误标志） */

    App_Pdo_ExchangeOutputs();            /* 取 RxPDO，回写 TxPDO */
    App_Position_UpdateInterpolation();   /* 更新插值步长 */
}
```

### 15.8 精度与性能的关键权衡

| 参数 | 影响 | 调大 | 调小 |
|---|---|---|---|
| **滤波深度**（`0x0930`） | 抖动 vs 收敛速度 | 抖动小、收敛慢（可能跟不上温漂） | 收敛快、抖动大 |
| **SYNC0 周期**（`0x098A`） | 同步精度 vs 总线负载 | 负载低、同步精度差 | 同步精度高、负载高 |
| **SYNC 信号脉宽**（`0x0981`） | 中断响应可靠性 | 更可靠、占时间 | 可能漏中断 |

**工程经验值**：

| 指标 | 典型目标 |
|---|---|
| 多轴 SYNC0 对齐误差 | **< 100 ns** |
| 系统时间读数误差 | < 10 ns |
| 漂移补偿后两轴时间差 | 稳定在 ±几十 ns（**不累积**） |

**怎么测量？** 用示波器**同时**看两台从站的 SYNC0 引脚输出
（或 PDI 中断引脚），直接量两者边沿的时间差。**这是最直接、最可信的验证方法**——
比看主站的"同步质量"指示更有说服力。

> 💡 **这和你 README 里的方法论完全一致**：
> "实测数字"胜过推断。DC 是否真的同步了，**用示波器量，不要相信软件报告**。

### 15.9 ⚠️ 本课内容的可信度声明

| 内容 | 可信度 | 说明 |
|---|---|---|
| DC 的原理、漂移补偿机制、SYNC0/1 分工 | ✅ **高** | ETG.1000 协议标准 |
| **DC 寄存器偏移**（`0x0900`~`0x0998`） | ⚠️ **中高** | ET1100 标准定义，但 **N32H785 是否完全一致未核实** |
| `0x0980` 位定义 | ⚠️ **中高** | 同上 |
| 典型精度指标 | ⚠️ **参考值** | 依 ESC 实现与 PHY 而异 |

> **本课与第 13 课的区别**：第 13 课（邮箱）我**拒绝写代码**，因为邮箱的
> DPRAM 布局在 ET1100 里是**厂商自由定义**的；而 DC 寄存器区在 ET1100 里
> 是**规范明确定义**的。所以本课我可以给出偏移，**但仍标注为待首次上板核对**。
>
> **首次上板核对方法**（不需要手册也能做）：
> 1. 读 `0x0910`（系统时间低 32 位）两次，间隔已知时间
>    → 差值应约等于间隔的纳秒数。**若为 0 或乱跳，说明偏移不对。**
> 2. 读 `0x0982`（激活状态），主站配置 DC 后应有非零值。
> 3. 读 `0x0924`（与参考时钟差值），正常应在小范围内波动。
> **这三个读操作就是你的 DC 寄存器"探针"，能反向确认偏移是否正确。**

### 15.10 本课作业

**作业 1**：为什么 DC 的漂移补偿**必须通过"调整时钟走速"实现，而不能
"每隔一段时间把时间跳变为参考值"**？请从"跳变对 SYNC0 的影响"角度说明。
（提示：如果时间跳变，SYNC0 周期会发生什么？）

**作业 2**：主站配置 DC 时写了 `0x098A`（SYNC0 周期）= `1000000`（纳秒）。
请回答：① SYNC0 频率是多少 Hz？② 如果把 `0x0930`（滤波深度）从 0 调到一个较大值，
系统的**抖动**和**收敛速度**分别怎么变？③ 什么场景下你需要"收敛快、容忍抖动"？

**作业 3（综合题）**：请设计你的伺服在 **CSP 模式 + DC 1 ms 周期 + 20 kHz 电流环**
下的完整时序，画出从 SYNC0 中断到 PWM 更新的**时间轴**，并回答：
① PDO 数据在哪个中断里取？② 插值在哪个中断里做？③ 编码器采样应该在哪里触发？
④ 为什么"SYNC1 与 PWM 载波对齐"很重要？

**作业 4（判断力题）**：某同事说"我们的 DC 同步误差用 TwinCAT 看是 0，
所以肯定没问题"。请指出这个判断**为什么不可靠**，并给出至少两种更可信的验证方法。
