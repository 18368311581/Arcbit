# Arcbit 外部Flash与游戏存储参考

`FLASH_DOC/`是W25Q64外部资源、地址清单和烧录工具的正式目录。[`flash_resources.json`](flash_resources.json)是地址与保留区的唯一机器可读权威；源码和其他文档不得另行分配地址。

当前Arcbit v3.1（修改日期`260901`）只使用两份外部字库，存档格式仍为`AppProfile v5 / GameCatalog v2 / GameDataStore v1`（`P5/C2`）。GameId 1～9已占用，仅GameId 10可用；GameId 9层叠消融直接使用既有固定槽，Flash布局和外部资源未变化，不需要资源重烧。只有新增外部静态资源时才修改JSON并重新烧录。

## 资源检查与烧录

1. 将资源文件放入`FLASH_DOC/`或其子目录。
2. 在[`flash_resources.json`](flash_resources.json)中增加唯一资源项。
3. 双击[`一键烧录资源.cmd`](一键烧录资源.cmd)。
4. 工具检查文件、地址、4 KB对齐、重叠和越界，再通过Keil外部Loader烧录并校验。

资源项格式：

```json
{
  "name": "资源唯一名称",
  "file": "相对于FLASH_DOC的路径",
  "address": "0x00100000"
}
```

固定规则：

- W25Q64容量为`0x00800000`，有效偏移为`0x00000000`～`0x007FFFFF`。
- `format_version`固定为十进制`1`，`program_address_base`固定为`"0x90000000"`。
- 地址和大小统一使用带引号的十六进制字符串；资源起始地址按4 KB对齐，占用范围按4 KB向上取整。
- 资源不得重叠、越界或进入`reserved_regions`，文件路径必须位于本目录内。
- `0x00700000`～`0x007FEFFF`保留给玩家和游戏数据；`0x007FF000`为硬件自检扇区。

只检查清单并生成烧录镜像：

```powershell
& 'C:\Users\jason\AppData\Local\Programs\Python\Python312\python.exe' '.\program_resources.py'
```

生成物位于`build/`：`flash_resources.hex`、`flash_manifest.json`和`program.log`。烧录日志必须出现`Verify OK`才算成功；生成HEX、内部固件构建、`Erase Done`或`Programming Done`均不能替代校验。

烧录使用已验证的ATK-DAP/CMSIS-DAP。操作时不接6 V主电源，由确认具有3.3 V输出能力的DAP给PCB的`3V3`网供电；不得把其他调试器的VTref输入当作电源输出。

## 玩家与游戏存储布局

以下均为W25Q64内部偏移。`N=0..5`表示玩家，`G`表示稳定GameId。

| 范围或偏移 | 大小 | 用途 |
| --- | ---: | --- |
| `0x00700000`～`0x00701FFF` | 8 KB | 玩家目录A |
| `0x00702000`～`0x00703FFF` | 8 KB | 玩家目录B |
| `0x00704000`～`0x0070FFFF` | 48 KB | 玩家管理扩展预留 |
| `0x00710000 + N×0x20000` | 每玩家128 KB | 玩家独立分区 |
| 玩家基址`+0x0000 / +0x2000` | 各8 KB | AppProfile A/B |
| 玩家基址`+0x4000 / +0x6000` | 各8 KB | GameId 1快照A/B |
| 玩家基址`+0x8000 + (G-2)×0x2000` | 4 KB | GameId 2～10快照A |
| 上一行地址`+0x1000` | 4 KB | 对应快照B |
| 玩家基址`+0x1A000 / +0x1B000` | 各4 KB | GameCatalog A/B |
| 玩家基址`+0x1C000 / +0x1E000` | 各8 KB | GameDataStore A/B |
| `0x007D0000`～`0x007FEFFF` | `0x2F000` | 全局后备区 |
| `0x007FF000`～`0x007FFFFF` | 4 KB | 硬件自检扇区 |

新增GameId 10只需在现有公式对应的槽中保存快照，并使用`GameDataStore`中同ID条目；不得修改地址清单。每游戏长期数据负载最多508 B，快照最多1536 B。

## 双槽与格式约束

- 当前公共格式为`P5/C2`：`AppProfile v5`为32 B，`GameCatalog v2`为256 B，`GameDataStore v1`包含10个固定游戏条目。
- `SaveRecordHeader`布局保持冻结。记录使用A/B双槽、序号、长度、CRC和提交标记，新槽完成回读前保留旧槽。
- 读取时若仅一槽有效，底层静默复制并校验另一槽；修复失败仍可从健康槽读取，并在系统状态中记录退化。
- 单游戏必须自行维护长期数据版本、快照magic/版本、长度和内容校验；单游戏数据失效不得阻止其他游戏加载。
- 玩家删除和恢复出厂只擦除玩家目录及6个玩家分区，保留字库、全局后备区和硬件自检扇区。

## 外部Loader

`loader/build/W25Q64_STM32F411_SPI2.FLM`运行于STM32F411 RAM，通过PB12～PB15的SPI2操作W25Q64，仅供电脑经SWD烧录外部资源，不替代正式固件的`flash.c/.h`。

Loader虚拟地址为`0x90000000`～`0x907FFFFF`。工具会自动把JSON中的W25Q64偏移加上该基地址；固件运行时仍使用`0x00000000`～`0x007FFFFF`内部偏移。
