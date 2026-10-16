# Arcbit

基于 **STM32F411RET6** 的自制掌上游戏机，包含裸机 C/C++ 固件、PCB 工程、外壳模型和外部 Flash 资源工具。

当前版本：**v3.1**（固件修改日期 `260901`），已完成九款游戏的集成。项目已有的开发记录确认该版本全量构建、内部固件烧录及实板验收通过；这些是版本历史结果，不代表每次仓库更新均重新执行硬件验收。

## 硬件与功能

| 项目 | 配置 |
| --- | --- |
| 主控 | STM32F411RET6，100 MHz |
| 屏幕 | ILI9341，320 × 240 |
| 外部存储 | W25Q64，8 MiB |
| 软件 | 裸机 C/C++，Keil MDK / ArmClang |
| 玩家 | 最多 6 名，设置、记录与快照独立 |
| 存档 | A/B 双槽、序号、CRC、提交标记与回读校验 |

公共功能包括 HOME、游戏入口、模式选择、MENU、暂停、结算、跨玩家记录、主题、声音设置和保存后关机。游戏使用静态内存与局部刷新，不依赖 RTOS 或完整屏幕帧缓冲。

## 游戏

| ID | 游戏 |
| ---: | --- |
| 1 | 普通贪吃蛇 |
| 2 | 竞技贪吃蛇 |
| 3 | 五子棋 AI |
| 4 | 2048 |
| 5 | 飞翔小鸟 |
| 6 | 俄罗斯方块 |
| 7 | 扫雷 |
| 8 | 第一人称 3D 迷宫 |
| 9 | 层叠消融 |

方向键用于导航或移动，Y 确认，X 返回；HOME、MENU、PAUSE 由公共应用层处理。具体游戏操作以机内提示为准。

## 目录

| 路径 | 内容 |
| --- | --- |
| `SRC/Core/` | 应用、游戏、输入、显示、声音和存档源码 |
| `SRC/Drivers/` | STM32 HAL、CMSIS 等第三方组件 |
| `SRC/MDK-ARM/SRC.uvprojx` | Keil 工程，Target 为 `SRC` |
| `PCB/game_play.eprj2` | 嘉立创 EDA 专业版工程 |
| `3D/` | CATIA 模型、STL 与 3MF 文件 |
| `FLASH_DOC/` | 外部字库、地址清单、Loader 与烧录工具 |

## 构建固件

1. 准备 Windows、Keil MDK 和 STM32F4 对应设备支持。v3.1 的历史构建使用 ArmClang 6.16。
2. 打开 `SRC/MDK-ARM/SRC.uvprojx`，选择 Target `SRC`。
3. 执行 **Rebuild All**。构建前事件调用 `generate_gbk_ui_text.ps1`，将 UTF-8 的 `ui_text.cpp` 转换为 `Generated/ui_text_gbk.cpp`；生成文件不应手动编辑。
4. 通过与实板匹配的 SWD 调试器下载内部固件。调试器、设备包和本机工具路径需按实际环境配置。

仓库不跟踪对象文件、构建日志、个人 Keil 配置和生成的内部固件；请从源码构建。保留的 `SRC.sct` 是链接布局文件。

v3.1 历史全量构建结果：`0 Error / 0 Warning`；Code / RO-data / RW-data / ZI-data 分别为 **188496 / 20896 / 48 / 59408 B**，最大已知静态栈深 **800 B + Unknown**，主栈预留 4096 B。

## 外部 Flash

首次使用需要准备两份 GBK 字库。资源地址及保留区域仅以 [`flash_resources.json`](FLASH_DOC/flash_resources.json) 为准，不能通过猜测地址烧录。v3.1 与 v3.0 的外部资源布局相同，已有正确资源的设备无需为 v3.1 重烧字库。

使用方法见 [`FLASH_DOC/README.md`](FLASH_DOC/README.md)。资源工具依赖 Python 和本机 Keil 环境；运行 `FLASH_DOC/一键烧录资源.cmd` 可检查资源并烧录，必须以 **Verify OK** 作为外部烧录成功依据。

烧录前按 Flash 文档核对供电方式、调试器和资源范围，避免覆盖玩家存档与硬件自检区。

## 继续开发

详细架构、接口、存储约束及验收项目见 [`C++ APP开发进度与资料.md`](SRC/C++%20APP开发进度与资料.md) 和 [`AGENTS.md`](AGENTS.md)。当前 GameId 1～9 已固定，预留的第十款游戏只能使用 ID 10。硬件、底层接口、存储布局与既有游戏格式保持稳定。

固件改动应分别记录全量构建、离线规则验证、内部固件烧录和实板验收；这些检查不能相互代替。

## 许可证

本项目中由江沛宸（Jiang Peichen / Inseven）拥有版权的原创内容采用 [MIT License](LICENSE)。使用、修改和分发时请保留版权与许可声明。

第三方代码、字库及其他外部材料不因根目录 MIT 许可证而改变原授权，具体说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
