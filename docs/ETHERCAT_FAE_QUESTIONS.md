# N32H785EC EtherCAT 从站开发 —— 厂商技术咨询清单

> **用途**：本文件可直接发送给 **国民技术 FAE** 与 **裕太微 FAE**。
> **背景**：基于 N32H785 开发 **PMSM 伺服从站**（FOC + CiA402），使用片内 ESC。
> **当前工程**：`Lwip_Ping_Test`（Keil MDK，ARMCC V5.06，N32H78x target）。
> **填答方式**：请在每个问题下的 `答：` 后直接补充。带 ★ 的是**阻塞 PCB 投板**的问题。

---

## 0. 我方已确认的事实（供 FAE 参考，避免重复确认）

以下信息均来自 **SDK 源码**（`NNS.N32H7xx_Library` 标准外设库），非推测：

| 项 | 值 | 证据位置 |
|---|---|---|
| ESC 寄存器基地址 | `0x400B0000` | `n32h7xx.h:2877,3081`（`AHB9PERIPH_BASE = PERIPH_BASE + 0xB0000`，`PERIPH_BASE = 0x40000000`） |
| ESC Wrapper 基地址 | `0x400C0000` | `n32h7xx.h:3080` |
| ESC 专用中断 | `ESC_OPB_IRQn=187`、`ESC_SYNC0_IRQn=188`、`ESC_SYNC1_IRQn=189`、`ESC_WRP_IRQn=190` | `n32h7xx.h:348-351` |
| ESC 时钟门控（可分核） | `RCC_AHB9_PERIPHEN_M7_ESC` / `_M4_ESC` / `_M7_ESCLP` / `_M4_ESCLP` | `n32h7xx_rcc.h:804-807` |
| ESC 复位 | `RCC_AHB9_PERIPHRST_ESC` | `n32h7xx_rcc.h:310` |
| ESC 内核时钟源 | `SYSBUSDIV` / `PLL1B` / `PLL2B` / `PLL3A` / `PLL3C` | `n32h7xx_rcc.h:2012-2018` |
| ESC 内核时钟配置函数 | `RCC_ConfigETHERCATKerClk(src, div)` | `n32h7xx_rcc.c:6733` |
| AHB9 总线时钟门控 | `RCC_EnableCFG4PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ENABLE)` | `n32h7xx_rcc.c:6910`，`n32h7xx_rcc.h:999` |
| **100 MHz 硬性要求** | 官方注释：`You need to make sure that the ETHERCAT kernel clock frequency is 100MHz` | `n32h7xx_rcc.c:6731` |
| AHB9 时钟 = 系统总线时钟 | `RCC_Clocks->AHB9ClkFreq = RCC_Clocks->SysBusDivClkFreq;` | `n32h7xx_rcc.c:7766` |

**我方统计**：整个标准外设库中，涉及 EtherCAT 的**公开 API 只有 `RCC_ConfigETHERCATKerClk()` 一个**，
没有任何 ESC 寄存器定义、SM/FMMU 配置、ESM 状态机或邮箱协议实现。

---

## 1. ESC 基本架构与寄存器兼容性 ★

**Q1.1（最关键）** 片内 ESC 的寄存器布局是否与 **Beckhoff ET1100** 兼容？
具体希望确认以下偏移是否一致（我方拟直接按 ET1100 布局编写驱动）：

| 寄存器 | ET1100 偏移 | N32H785 是否一致？ |
|---|---|---|
| ESC Type / Revision / Build | `0x0000` / `0x0001` / `0x0002` | |
| FMMU 数量 / SM 数量 / RAM 大小 | `0x0004` / `0x0005` / `0x0006` | |
| Features（含 DC 使能位 bit0） | `0x0008` | |
| Configured Station Address | `0x0010` | |
| AL Control / AL Status / AL Status Code | `0x0120` / `0x0130` / `0x0134` | |
| AL Event Request / Mask | `0x0220` / `0x0204` | |
| Watchdog Divider / Time PDI | `0x0400` / `0x0410` | |
| EEPROM Configuration / Control / Addr / Data | `0x0500` / `0x0502` / `0x0504` / `0x0508` | |
| FMMU0 配置块（16 B/个） | `0x0600` | |
| SM0 配置块（8 B/个） | `0x0800` | |
| 过程数据 RAM 起始 | `0x1000` | |

答：

**Q1.2** 若**不完全兼容**，请提供**差异清单**或 **ESC 寄存器手册**（最关键的一份文档）。

答：

**Q1.3** ESC 的 **PDI（Process Data Interface）** 是哪一种？

- [ ] 并行/片内总线直连（CPU 通过 AHB9 直接访问 DPRAM）
- [ ] SPI
- [ ] 其他：________

答：

**Q1.4** 硬件资源规格请确认：

| 项 | 请求确认 |
|---|---|
| FMMU 数量 | |
| SyncManager 数量 | |
| 过程数据 RAM 大小 | |
| 是否支持 **64 位分布式时钟（DC）** | |
| 是否支持 SYNC0 / SYNC1 信号输出到**外部引脚** | |
| 是否支持 3 缓冲（Buffered）模式 | |
| 是否支持 EEPROM（I²C，含 ESI 存储） | |

答：

---

## 2. 时钟与复位 ★

**Q2.1** ESC 内核时钟必须**精确等于** 100 MHz，还是可在某范围内（如 100 MHz ± x%）？

答：

**Q2.2** 官方推荐的 100 MHz 时钟源配置是什么？

我方推算：因 `ESCSYSDIV` 分频链只有 2 的幂（1/2/4/8/16/32/64/128/256/512），
若系统总线为 300 MHz，则 `SYSBUSDIV` 路线只能得到 300/150/75 MHz，**无法得到 100 MHz**。

- **请确认上述推算是否正确**；
- 并请给出**推荐的 PLL 配置**（用 `PLL1B` / `PLL2B` / `PLL3A` / `PLL3C` 中的哪一路、
  具体 VCO 与分频参数），以得到精确 100 MHz。

答：

**Q2.3** `RCC_EnableCFG4PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ENABLE)` 是否是启用 ESC 访问的
**正确且必需**的步骤？是否还有其它前置条件（如某个 lock/unlock、或 PWR 相关使能）？

答：

**Q2.4** ESC 归 **M7** 还是 **M4** 使用，在软件/硬件配置上有何差异？
（我方倾向让 ESC 归 M4，由 M4 跑从站协议栈，M7 专注 FOC 电流环。）

答：

**Q2.5** 是否存在 **M7 与 M4 同时访问 ESC** 的合法用法？若不合法，请说明如何避免误访问。

答：

---

## 3. 网络端口与 PHY ★

**Q3.1** ESC 提供几个 **MII/RMII** 端口？（EtherCAT 从站通常需 2 个以支持级联与冗余环路）

答：

**Q3.2** 这些端口的引脚是**独立专用引脚**，还是**复用现有 ETH1/ETH2 的 RMII 引脚**？
请提供**引脚复用表**（Pin / AF 编号）。

答：

**Q3.3** RMII 参考时钟（50 MHz）由谁提供？ESC 输出给 PHY、还是 PHY 输出给 ESC？
时钟抖动对 DC（分布式时钟）精度的影响要求是什么？

答：

**Q3.4** ESC 能否读取 **PHY 的链路状态**（link up/down）？通过什么寄存器/接口？

答：

**Q3.5** 若在同一颗 MCU 上同时使用**通用以太网（现工程的 ETH1 + lwIP）**与 **EtherCAT ESC**，
是否被支持？需要几个 PHY？有无已知冲突？

答：

---

## 4. PHY 选型适配（请转 裕太微 FAE）

**Q4.1** **YT8522H** 是否已用于 EtherCAT 从站设计？是否有应用笔记或参考设计？

答：

**Q4.2** 其**转发延迟（forwarding latency）** 典型值与最大值是多少？
EtherCAT 要求该延迟低且**一致**（典型目标 < 500 ns）。

答：

**Q4.3** 如何将其配置为 EtherCAT 要求的确定性工作模式：

- 强制 **100 Mbps 全双工**（或自协商完成后锁定）
- **关闭 EEE**（节能以太网）
- **关闭 Auto-MDIX**
- **关闭 Auto-Downshift**（禁止自动降速到 10 Mbps）

请提供对应的**寄存器地址与值**。

答：

**Q4.4** 是否存在需要特别注意的**时钟抖动、源同步时钟（TXC/RXC）延迟**或上电时序问题？

答：

---

## 5. 软件资源（决定项目工作量） ★

**Q5.1** 是否提供 **ESC 寄存器定义头文件**？在哪获取？
（文件名待贵司确认，例如可能形如 `n32h7xx_ethercat.h`；我方目前只能自行按 ET1100 布局推测。）

答：

**Q5.2** 是否提供 **EtherCAT 从站协议栈** 或 **ETG SSC（Slave Stack Code）移植示例**？
（ETG 官方 SSC 需针对具体 ESC 做移植层适配，请提供该移植层或适配说明。）

答：

**Q5.3** 是否提供 **N32H785EC 的 EtherCAT 从站示例工程**（能进 OP 态的最小例程）？

答：

**Q5.4** 请提供 **N32H7xx 用户手册的 EtherCAT 章节**（或独立 ESC 应用笔记），
以及**含 EtherCAT 内容的 N32H785 数据手册**版本。

答：

**Q5.5** 是否提供示例 **ESI（XML）文件** 及 **EEPROM 烧录工具/流程**？

答：

**Q5.6** 是否有 **ETG 一致性测试（ETG.1020）** 的通过报告或已知限制说明？

答：

---

## 6. 中断

**Q6.1** `ESC_SYNC0_IRQn`(188) 与 `ESC_SYNC1_IRQn`(189) 的**触发源**分别是什么？
如何配置其周期？

答：

**Q6.2** `ESC_OPB_IRQn`(187) 与 `ESC_WRP_IRQn`(190) 各自在什么事件下触发（OPB / Wrapper 的具体含义）？

答：

**Q6.3** ESC 相关中断的**推荐优先级**？与 PWM/电流环中断并存的注意事项？

答：

---

## 7. 我方计划（供 FAE 评估可行性）

1. **MCU**：N32H785EC 自建板（LDO 供电，非官方 EVAL 的 SMPS）
2. **网络**：2 个 PHY 接 ESC 双端口
3. **双核**：M4 跑 EtherCAT 从站栈 + 邮箱(CoE) + 对象字典；M7 跑 FOC(16~20 kHz) + CiA402
4. **目标**：CiA402 伺服从站，支持 **CSP / CSV / CST** 三种模式，DC 同步
5. **软件路线**：ETG SSC 生成协议栈 → 自写 ESC 寄存器访问 BSP → 接 FOC

请评估该方案是否有阻塞性风险，并指出我方遗漏的要点。

答：

---

## 8. 优先级汇总（若 FAE 时间有限，请优先回答这些）

| 优先级 | 编号 | 问题 | 阻塞什么 |
|---|---|---|---|
| **P0** | Q1.1 / Q1.2 | ESC 寄存器是否 ET1100 兼容 / 提供寄存器手册 | **全部软件开发** |
| **P0** | Q3.1 / Q3.2 | ESC 端口数量与引脚 | **PCB 投板** |
| **P0** | Q5.2 / Q5.3 | 是否提供 SSC 移植层 / 示例工程 | **项目工作量评估** |
| **P1** | Q2.2 | 100 MHz 时钟的推荐 PLL 配置 | DC 精度 |
| **P1** | Q4.1 / Q4.3 | YT8522H 适配性与配置值 | PHY 选型 |
| **P2** | Q6.1 | SYNC0 中断配置 | 实时性设计 |
