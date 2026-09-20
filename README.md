# EtherCAT 学习与 N32H785 工程落地

从零学习 EtherCAT 协议原理，并落到 **N32H785EC**（Cortex-M7 + M4 双核，**片内集成 ESC**）上做
**PMSM 伺服从站**（FOC + CiA402，支持 CSP / CSV / CST）。

> 本仓库是一套**教学 + 工程**资料：22 万字节文档、4 个可编译链接的驱动模块、2 个验证工具。
> **`docs/ETHERCAT_HANDOFF.md` 是总入口** —— 先读它。

---

## 目录结构

```
docs/
├── ETHERCAT_HANDOFF.md          交接简报：总入口，读这个
├── ETHERCAT_LEARNING_NOTES.md   教学主体，2198 行 / 15 章（第 1~9 课）
└── ETHERCAT_FAE_QUESTIONS.md    厂商技术咨询清单（27 问，可直接发送）
src/
├── Mcal/                        硬件抽象层（平台相关）
│   ├── inc|src/Mcal_Esc_Init.*  ESC 上电：AHB9 时钟、复位、100 MHz 内核时钟
│   └── inc|src/Mcal_Esc_Reg.*   ESC 寄存器共享访问层（8/16/32/64 位）
└── User_App/                    应用层（与芯片无关）
    ├── inc/App_Esc_Reg.h        ESC 寄存器地图
    ├── inc|src/App_Esc_Esm.*    ESM 状态机（INIT→PRE-OP→SAFE-OP→OP）
    └── inc|src/App_Cia402.*     CiA402 驱动状态机（IEC 61800-7-201）
tools/
├── cia402_table_check.py        CiA402 状态字对真值表交叉校验（PASS）
└── cia402_behaviour_check.c     行为测试（20+ 断言，需在 ARM 目标上运行）
CHECKSUMS.txt                    SHA256 校验清单
```

---

## 课程进度

| 课 | 主题 | 状态 |
|---|---|---|
| 1 | Frame 结构 / WKC / 三种寻址 | ✅ |
| 2 | ESC 片内集成 + 硬件蓝图 | ✅ |
| 3 | 数据通路全景（ESM/CiA402/DC） | ✅ |
| 4 | 对象字典 + PDO 映射（`0x1600`/`0x1A00`） | ✅ |
| 5 | SM / FMMU 配置 | ✅ |
| 6 | ESM 状态机 | ✅ |
| 7 | CoE 邮箱 + 对象字典（SDO / Abort 码） | ✅ |
| 8 | CiA402 + FOC 对接（CSP/CSV/CST） | ✅ |
| 9 | DC 分布式时钟（漂移补偿 / SYNC0·SYNC1） | ✅ |
| 10 | TwinCAT 主站实操 | ❌ 需硬件 |
| 11 | ESC 寄存器 + 裸机实验 | ❌ 需硬件 |
| 12 | 性能调优 | ❌ 需硬件 |

**协议原理教学（第 1~9 课）已全部完成。**

---

## 验证状态

| 项 | 结果 |
|---|---|
| 编译（ARMCC V5.06，Cortex-M7，工程精确参数） | ✅ 全部 0 error 0 warning |
| 链接（`armlink --strict`，含 SDK 时钟模块） | ✅ 退出码 0 |
| CiA402 状态字真值表校验（`tools/cia402_table_check.py`） | ✅ **PASS** |
| CiA402 行为测试（`tools/cia402_behaviour_check.c`） | ⚠️ 编译通过，**未执行**（需 ARM 目标/仿真器） |
| 整工程链接 | ⚠️ 未做（需用户本机 Keil 授权） |

---

## ⚠️ 重要：一个未验证的根本假设

`src/User_App/inc/App_Esc_Reg.h` 中的寄存器偏移**建立在以下假设之上**：

> **N32H785 片内 ESC 的寄存器布局与 Beckhoff ET1100 兼容。**

**该假设来自行业惯例，不是官方文档**（芯片 SDK 中没有任何 ESC 寄存器定义）。
上板前**必须逐条对照官方手册核对**。详见 `docs/ETHERCAT_LEARNING_NOTES.md` §11.4。

**与芯片无关、可长期复用的部分**：ESM 状态机的逻辑结构、CiA402 状态机、
以及第 1~9 课的全部协议知识（这些属于 ETG.1000 / IEC 61800-7-201 标准）。

---

## 怎么用

### 方式一：只学协议（无需硬件）

直接读 `docs/ETHERCAT_LEARNING_NOTES.md`，从 §1 顺序往下。
每课末尾有作业，**建议先做题再看答案**（第 4 课答案在 §11.1、第 6 课在 §12.8、第 7 课在 §13.11）。

### 方式二：接入 N32H785 工程

把这批文件按目录对应关系拷进你的 Keil 工程（路径见上），并在 `LWIP_PING.uvprojx` 中登记
这 4 个 `.c` 文件：

```
src/Mcal/src/Mcal_Esc_Init.c
src/Mcal/src/Mcal_Esc_Reg.c
src/User_App/src/App_Esc_Esm.c
src/User_App/src/App_Cia402.c
```

**注意**：这些模块目前**无调用方**，加入构建后行为零变化——刻意如此，等硬件到位再逐个接通。
接通顺序与验证方法见 `docs/ETHERCAT_HANDOFF.md` 第 5 节。

### 方式三：让 AI 接着教

在 DEEPSEEK HARNESS（或其他支持读文件的 AI）里说：

> 读 `docs/ETHERCAT_HANDOFF.md`，按里面的「下一步该做什么」继续教我

`ETHERCAT_HANDOFF.md` 是**自包含的会话交接简报**，包含用户画像、已完成内容、
平台约束、已核实事实、未验证假设、已知缺陷、下一步计划 —— 让新会话无需原始对话即可接续。

---

## 代码风格约定

- 每个函数必须有 STM32 风格头注释（`@name` / `@brief` / `@param` / `@retval`，英文）
- 命名：下划线分隔 + 词首大写（`Led_On`）；禁止 `s_` / `g_` 前缀
- 禁止 banner 分隔注释；函数内注释只写一行
- 厂商 SDK 宏/寄存器名保持原样

---

## 芯片关键事实（已核对源码）

| 项 | 值 | 证据 |
|---|---|---|
| ESC 寄存器基地址 | `0x400B0000` | `n32h7xx.h` `ETHERCAT_BASE` |
| ESC 专用中断 | 187 / 188 / 189 / 190（OPB / SYNC0 / SYNC1 / WRP） | `n32h7xx.h:348-351` |
| ESC 内核时钟 | **必须精确 100 MHz** | `n32h7xx_rcc.c:6731` 官方注释 |
| ESC 时钟门控（可分核） | `RCC_AHB9_PERIPHEN_M7_ESC` / `_M4_ESC` | `n32h7xx_rcc.h:804-807` |
| 总线时钟门控 | `RCC_EnableCFG4PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ENABLE)` | `n32h7xx_rcc.c:6910` |

**推论**：300 MHz 系统总线下 `SYSBUSDIV` 分频链（只有 2 的幂）**凑不出 100 MHz**，
必须用独立 PLL 输出。详见 `docs/ETHERCAT_LEARNING_NOTES.md` §2.6。
