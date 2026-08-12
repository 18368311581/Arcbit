from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


FLASH_SIZE = 0x00800000
PROGRAM_ADDRESS_BASE = 0x90000000
SECTOR_SIZE = 0x1000
HEX_RECORD_SIZE = 32


class ResourceError(Exception):
    pass


def parse_number(value: object, field: str) -> int:
    if isinstance(value, bool):
        raise ResourceError(f"{field} 不能是布尔值")
    if isinstance(value, int):
        number = value
    elif isinstance(value, str):
        text = value.strip()
        if re.fullmatch(r"0[xX][0-9a-fA-F]+", text):
            number = int(text, 16)
        elif re.fullmatch(r"[0-9]+", text):
            number = int(text, 10)
        else:
            raise ResourceError(f"{field} 必须是十进制整数或类似 \"0x00100000\" 的字符串")
    else:
        raise ResourceError(f"{field} 必须是整数或十六进制字符串")
    if number < 0:
        raise ResourceError(f"{field} 不能为负数")
    return number


def ranges_overlap(start_a: int, end_a: int, start_b: int, end_b: int) -> bool:
    return start_a < end_b and start_b < end_a


def sector_end(address: int, size: int) -> int:
    return (address + size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)


def load_config(config_path: Path) -> tuple[dict, list[dict]]:
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ResourceError(f"找不到清单：{config_path}") from exc
    except json.JSONDecodeError as exc:
        raise ResourceError(f"JSON 格式错误：第 {exc.lineno} 行，第 {exc.colno} 列：{exc.msg}") from exc

    if config.get("format_version") != 1:
        raise ResourceError("format_version 目前必须为 1")
    if parse_number(config.get("flash_size"), "flash_size") != FLASH_SIZE:
        raise ResourceError("flash_size 必须为 \"0x00800000\"")
    if parse_number(config.get("program_address_base"), "program_address_base") != PROGRAM_ADDRESS_BASE:
        raise ResourceError("program_address_base 必须为 \"0x90000000\"")

    reserved = []
    for index, item in enumerate(config.get("reserved_regions", [])):
        if not isinstance(item, dict):
            raise ResourceError(f"reserved_regions[{index}] 必须是对象")
        name = str(item.get("name", "")).strip()
        address = parse_number(item.get("address"), f"reserved_regions[{index}].address")
        size = parse_number(item.get("size"), f"reserved_regions[{index}].size")
        if not name or size == 0:
            raise ResourceError(f"reserved_regions[{index}] 的 name 和 size 不能为空")
        if address % SECTOR_SIZE != 0 or size % SECTOR_SIZE != 0:
            raise ResourceError(f"保留区 {name} 的 address 和 size 必须按 4 KB 对齐")
        if address + size > FLASH_SIZE:
            raise ResourceError(f"保留区 {name} 超出 W25Q64 容量")
        reserved.append({"name": name, "start": address, "end": address + size})

    resources_value = config.get("resources")
    if not isinstance(resources_value, list):
        raise ResourceError("resources 必须是数组")

    root = config_path.parent.resolve()
    resources = []
    names = set()
    for index, item in enumerate(resources_value):
        if not isinstance(item, dict):
            raise ResourceError(f"resources[{index}] 必须是对象")
        name = str(item.get("name", "")).strip()
        file_value = item.get("file")
        if not name or not isinstance(file_value, str) or not file_value.strip():
            raise ResourceError(f"resources[{index}] 必须填写 name 和 file")
        if name in names:
            raise ResourceError(f"资源名称重复：{name}")
        names.add(name)

        relative_file = Path(file_value)
        if relative_file.is_absolute():
            raise ResourceError(f"资源 {name} 的 file 必须是 FLASH_DOC 内的相对路径")
        file_path = (root / relative_file).resolve()
        try:
            file_path.relative_to(root)
        except ValueError as exc:
            raise ResourceError(f"资源 {name} 的 file 不能指向 FLASH_DOC 之外") from exc
        if not file_path.is_file():
            raise ResourceError(f"找不到资源文件：{file_value}")

        address = parse_number(item.get("address"), f"resources[{index}].address")
        size = file_path.stat().st_size
        if size == 0:
            raise ResourceError(f"资源文件不能为空：{file_value}")
        if address % SECTOR_SIZE != 0:
            raise ResourceError(f"资源 {name} 的 address 必须按 4 KB 对齐")
        end = sector_end(address, size)
        if end > FLASH_SIZE:
            raise ResourceError(f"资源 {name} 超出 W25Q64 容量")

        for region in reserved:
            if ranges_overlap(address, end, region["start"], region["end"]):
                raise ResourceError(f"资源 {name} 与保留区 {region['name']} 重叠")
        for other in resources:
            if ranges_overlap(address, end, other["address"], other["sector_end"]):
                raise ResourceError(f"资源 {name} 与资源 {other['name']} 的扇区范围重叠")

        resources.append(
            {
                "name": name,
                "relative_file": relative_file.as_posix(),
                "file_path": file_path,
                "address": address,
                "size": size,
                "sector_end": end,
            }
        )

    resources.sort(key=lambda item: item["address"])
    return config, resources


def ihex_record(address: int, record_type: int, data: bytes) -> str:
    body = bytes((len(data), (address >> 8) & 0xFF, address & 0xFF, record_type)) + data
    checksum = (-sum(body)) & 0xFF
    return ":" + (body + bytes((checksum,))).hex().upper()


def build_hex(resources: list[dict], output_path: Path) -> list[dict]:
    manifest = []
    current_upper = None
    with output_path.open("w", encoding="ascii", newline="\n") as output:
        for resource in resources:
            digest = hashlib.sha256()
            absolute = PROGRAM_ADDRESS_BASE + resource["address"]
            remaining = resource["size"]
            with resource["file_path"].open("rb") as source:
                while remaining:
                    lower = absolute & 0xFFFF
                    upper = absolute >> 16
                    if upper != current_upper:
                        output.write(ihex_record(0, 4, upper.to_bytes(2, "big")) + "\n")
                        current_upper = upper
                    chunk_size = min(HEX_RECORD_SIZE, remaining, 0x10000 - lower)
                    data = source.read(chunk_size)
                    if len(data) != chunk_size:
                        raise ResourceError(f"读取资源失败：{resource['relative_file']}")
                    digest.update(data)
                    output.write(ihex_record(lower, 0, data) + "\n")
                    absolute += chunk_size
                    remaining -= chunk_size
            manifest.append(
                {
                    "name": resource["name"],
                    "file": resource["relative_file"],
                    "address": f"0x{resource['address']:08X}",
                    "size": resource["size"],
                    "sha256": digest.hexdigest(),
                }
            )
        output.write(ihex_record(0, 1, b"") + "\n")
    return manifest


def write_manifest(manifest: list[dict], output_path: Path) -> None:
    data = {"format_version": 1, "resources": manifest}
    output_path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def set_text(root: ET.Element, path: str, value: str) -> None:
    element = root.find(path)
    if element is None:
        raise ResourceError(f"Keil 工程模板缺少字段：{path}")
    element.text = value


def create_keil_project(project_root: Path, build_dir: Path, loader_path: Path) -> Path:
    source_project = project_root / "SRC" / "MDK-ARM" / "SRC.uvprojx"
    source_options = project_root / "SRC" / "MDK-ARM" / "SRC.uvoptx"
    if not source_project.is_file() or not source_options.is_file():
        raise ResourceError("找不到现有 Keil 工程模板 SRC.uvprojx/SRC.uvoptx")
    if not loader_path.is_file():
        raise ResourceError(f"找不到外部 Flash Loader：{loader_path}")

    project_path = build_dir / "FLASH_RESOURCES.uvprojx"
    options_path = build_dir / "FLASH_RESOURCES.uvoptx"

    project_tree = ET.parse(source_project)
    project_root_xml = project_tree.getroot()
    loader_text = str(loader_path.resolve())
    flash_driver = (
        "UL2CM3(-S0 -C0 -P0 -FD20000000 -FC800 -FN1 "
        f"-FF0W25Q64_STM32F411_SPI2 -FS090000000 -FL00800000 -FP0({loader_text}))"
    )
    set_text(project_root_xml, "./Targets/Target/TargetName", "FLASH_RESOURCES")
    set_text(
        project_root_xml,
        "./Targets/Target/TargetOption/TargetCommonOption/FlashDriverDll",
        flash_driver,
    )
    set_text(project_root_xml, "./Targets/Target/TargetOption/TargetCommonOption/OutputDirectory", ".\\")
    set_text(project_root_xml, "./Targets/Target/TargetOption/TargetCommonOption/OutputName", "flash_resources.hex")
    set_text(project_root_xml, "./Targets/Target/TargetOption/TargetCommonOption/CreateHexFile", "0")
    set_text(project_root_xml, "./Targets/Target/TargetOption/Utilities/Flash1/DriverSelection", "-1")
    set_text(project_root_xml, "./Targets/Target/TargetOption/Utilities/Flash2", "BIN\\UL2CM3.DLL")
    set_text(project_root_xml, "./Targets/Target/TargetOption/Utilities/Flash3", "")
    groups = project_root_xml.find("./Targets/Target/Groups")
    if groups is not None:
        groups.clear()
    project_tree.write(project_path, encoding="utf-8", xml_declaration=True)

    options_tree = ET.parse(source_options)
    options_root_xml = options_tree.getroot()
    set_text(options_root_xml, "./Target/TargetName", "FLASH_RESOURCES")
    cmsis_dap_configured = False
    for entry in options_root_xml.findall("./Target/TargetOption/TargetDriverDllRegistry/SetRegEntry"):
        key = entry.findtext("Key", "")
        name = entry.find("Name")
        if name is None:
            continue
        if key == "CMSIS_AGDI":
            prefix = (name.text or "").split(" -FD", 1)[0]
            name.text = (
                f"{prefix} -FD20000000 -FC800 -FN1 -FF0W25Q64_STM32F411_SPI2.FLM "
                f"-FS090000000 -FL00800000 -FP0({loader_text})"
            )
            cmsis_dap_configured = True
        elif key == "ST-LINKIII-KEIL_SWO":
            prefix = (name.text or "").split(" -FD", 1)[0]
            name.text = (
                f"{prefix} -FD20000000 -FC800 -FN1 -FF0W25Q64_STM32F411_SPI2.FLM "
                f"-FS090000000 -FL00800000 -FP0({loader_text})"
            )
        elif key == "UL2CM3":
            name.text = (
                "UL2CM3(-S0 -C0 -P0 ) -FN1 -FC800 -FD20000000 "
                f"-FF0W25Q64_STM32F411_SPI2.FLM -FL00800000 -FS090000000 -FP0({loader_text})"
            )
    if not cmsis_dap_configured:
        raise ResourceError("现有 Keil 工程没有 CMSIS-DAP/CMSIS_AGDI 配置")
    options_tree.write(options_path, encoding="utf-8", xml_declaration=True)
    return project_path


def locate_uv4() -> Path:
    candidates = (
        Path(r"C:\Keil_v5\UV4\UV4.exe"),
        Path(r"C:\Keil\UV4\UV4.exe"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ResourceError("找不到 Keil UV4.exe；请确认 Keil MDK 安装位置")


def print_layout(resources: list[dict]) -> None:
    print("资源布局：")
    for item in resources:
        end = item["address"] + item["size"] - 1
        print(
            f"  {item['name']}: 0x{item['address']:08X} - 0x{end:08X} "
            f"({item['size']} 字节，{item['relative_file']})"
        )


def cmsis_dap_connected() -> bool:
    if os.name != "nt":
        return True
    result = subprocess.run(
        ["pnputil.exe", "/enum-devices", "/connected"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=15,
        check=False,
    )
    device_list = (result.stdout + result.stderr).upper()
    markers = (
        "CMSIS-DAP",
        "CMSIS_DAP",
        "DAPLINK",
        "ATK-DAP",
        "ATK DAP",
        "VID_04D8&PID_00DF",
        "VID_C251&PID_F001",
        "VID_0D28&PID_0204",
    )
    return any(marker in device_list for marker in markers)


def program(project_path: Path, log_path: Path, resource_bytes: int) -> None:
    if not cmsis_dap_connected():
        raise ResourceError("未检测到正点原子 CMSIS-DAP；请连接 DAP 和目标板后重试")
    uv4 = locate_uv4()
    # CMSIS-DAP 需要把每个 256 字节页分批送入 MCU RAM；写入与校验均比
    # ST-Link 慢，2.5 MB 资源在实板上约需 10 分钟，必须为完整校验留余量。
    timeout_seconds = max(300, min(3600, 300 + resource_bytes // 5000))
    print("正在通过 Keil + CMSIS-DAP 擦除、写入并校验外部 W25Q64……")
    process = subprocess.Popen(
        [str(uv4), "-f", str(project_path), "-o", str(log_path)],
    )
    try:
        return_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as exc:
        subprocess.run(
            ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
            capture_output=True,
            check=False,
        )
        raise ResourceError(f"Keil 在 {timeout_seconds} 秒内没有结束，已停止等待") from exc
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    if log:
        print(log.rstrip())
    if return_code not in (0, 1):
        raise ResourceError(f"Keil 烧录失败，退出码 {return_code}；详见 {log_path}")
    if "Verify OK" not in log:
        raise ResourceError(f"没有检测到“Verify OK”，不能确认烧录成功；详见 {log_path}")
    print("资源烧录及校验完成。")


def main() -> int:
    parser = argparse.ArgumentParser(description="校验 FLASH_DOC 资源清单并通过 SWD 写入 W25Q64")
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("flash_resources.json"))
    parser.add_argument("--program", action="store_true", help="生成镜像后调用 Keil 执行硬件烧录")
    args = parser.parse_args()

    try:
        config_path = args.config.resolve()
        _, resources = load_config(config_path)
        print_layout(resources)
        if not resources:
            raise ResourceError("resources 为空，请先在 flash_resources.json 中添加至少一个资源")

        build_dir = config_path.parent / "build"
        build_dir.mkdir(exist_ok=True)
        hex_path = build_dir / "flash_resources.hex"
        manifest_path = build_dir / "flash_manifest.json"
        manifest = build_hex(resources, hex_path)
        write_manifest(manifest, manifest_path)
        print(f"清单检查通过，已生成：{hex_path}")

        tool_dir = Path(__file__).resolve().parent
        loader_path = tool_dir / "loader" / "build" / "W25Q64_STM32F411_SPI2.FLM"
        keil_project = create_keil_project(tool_dir.parent, build_dir, loader_path)
        if args.program:
            program(keil_project, build_dir / "program.log", sum(item["size"] for item in resources))
        else:
            print("当前仅做离线检查，未连接或写入硬件。")
        return 0
    except ResourceError as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
