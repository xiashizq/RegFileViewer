#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
根据 conf 目录中的接口配置，生成可用于演示/自检的示例数据文件。

支持：
  * 中登 OFD 文件（交换协议 2.0 / 2.1 / 2.2）
  * 理财登定长文件（WDEP 1.1、WPID 1.1）

用法：
  python tools/make_demo.py
输出目录：
  conf/demo/
"""

from __future__ import annotations

import os
import sys

ENCODING = "gb18030"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONF = os.path.join(ROOT, "conf")
DEMO = os.path.join(CONF, "demo")


# --------------------------------------------------------------------------
# 极简 INI 解析（保留段名里的 ? * | 等字符）
# --------------------------------------------------------------------------
class Ini:
    def __init__(self, path: str) -> None:
        self.path = path
        self.sections: dict[str, dict[str, str]] = {}
        self.order: list[str] = []
        self._load()

    def _load(self) -> None:
        current = None
        with open(self.path, "r", encoding="utf-8-sig") as handle:
            for raw in handle:
                line = raw.rstrip("\r\n")
                stripped = line.strip()
                if not stripped or stripped[0] in ";#":
                    continue
                if stripped.startswith("[") and stripped.endswith("]"):
                    name = stripped[1:-1].strip()
                    if name not in self.sections:
                        self.sections[name] = {}
                        self.order.append(name)
                    current = self.sections[name]
                    continue
                if current is None or "=" not in stripped:
                    continue
                key, value = stripped.split("=", 1)
                value = value.strip()
                if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                    value = value[1:-1]
                current[key.strip()] = value

    def value(self, section: str, key: str, default: str = "") -> str:
        data = self.sections.get(section, {})
        if key in data:
            return data[key]
        lower = key.lower()
        for name, value in data.items():
            if name.lower() == lower:
                return value
        return default


def parse_fields(ini: Ini, section: str) -> list[dict]:
    count_text = ini.value(section, "count") or ini.value(section, "fieldcount")
    count = int(count_text.split("|")[0].strip())
    fields = []
    for index in range(1, count + 1):
        parts = ini.value(section, str(index)).split(",")
        if len(parts) < 4:
            raise ValueError(f"{section} 第 {index} 个字段定义无效")
        fields.append({
            "index": index,
            "type": parts[0].strip().upper(),
            "length": int(parts[1]),
            "decimals": int(parts[2]),
            "name_cn": parts[3].strip(),
            "name_en": parts[4].strip() if len(parts) > 4 else "",
        })
    return fields


# --------------------------------------------------------------------------
# 字段取值
# --------------------------------------------------------------------------
def sample_value(field: dict, row: int) -> str:
    name = field["name_en"]
    names = ["张三", "李四", "王五"]

    overrides = {
        "AppSheetSerialNo": f"AP202601010000{row + 1:04d}",
        "InvestorName": names[row % len(names)],
        "CertificateNo": f"11010119900101{row + 1:04d}",
        "CertificateType": "0",
        "IndividualOrInstitution": "1",
        "Sex": "1",
        "MobileTelNo": "13800138000",
        "TelNo": "010-88886666",
        "EmailAddress": "demo@example.com",
        "DepositAcct": f"622202020202{row + 1:04d}",
        "TAAccountID": f"123456789{row:03d}",
        "TransactionAccountID": f"0000000000000000{row + 1}",
        "DistributorCode": "999",
        "BranchCode": "0001",
        "BusinessCode": "020",
        "CurrencyType": "156",
        "FundCode": "000001",
        "CodeOfTargetFund": "000002",
        "TransactionDate": "20260101",
        "TransactionCfmDate": "20260102",
        "TransactionTime": "093000",
        "ReturnCode": "0000",
        "TASerialNO": f"TA202601010000{row + 1:02d}",
        "BusinessFinishFlag": "1",
        "ConfirmedVol": "0000000001234500",
        "ConfirmedAmount": "0000000005000000",
        "ApplicationVol": "0000000001000000",
        "ApplicationAmount": "0000000001000000",
        "NAV": "000000000123456789",
        "LargeRedemptionFlag": "0",
        "DetailFlag": "1",
        "Report_ID": f"RPT2026010100{row + 1:04d}",
        "WPCode": f"LC{row + 1:04d}",
        "WPRegCode": f"Z70000000000{row + 1:02d}",
        "Report_Type": "0001",
        "Report_SubType": "0001",
        "Report_Date": "20260101",
        "Sys_Title": "示例理财产品年度报告",
        "App_No": f"AP{row + 1:04d}",
        "Update_Time": "2026-01-01 09:30:00",
        "Operate_Flag": "1",
        "File_Name": "demo.txt",
        "CommonFlag": "1",
    }
    if name in overrides:
        return overrides[name]

    field_type = field["type"]
    length = field["length"]

    if field_type == "N":
        digits = str(1234 * (row + 1)).zfill(length)
        return digits[-length:]
    if field_type == "A":
        if "Date" in name and length == 8:
            return "20260101"
        if "Time" in name and length == 6:
            return "093000"
        return "0" * length

    # 字符型：用可读的 ASCII 占位，避免多字节截断
    text = f"{name[:8] or 'DEMO'}{row + 1}"
    return text


def encode_field(field: dict, value: str) -> bytes:
    length = field["length"]
    if field["type"] == "N":
        data = value.encode("ascii")
        return data.rjust(length, b"0")[-length:]

    data = value.encode(ENCODING)
    if len(data) > length:
        while data and len(data) > length:
            data = data[:-1]
        while data:
            try:
                data.decode(ENCODING)
                break
            except UnicodeDecodeError:
                data = data[:-1]
    return data.ljust(length, b" ")


def build_row(fields: list[dict], row: int) -> bytes:
    return b"".join(encode_field(field, sample_value(field, row)) for field in fields)


# --------------------------------------------------------------------------
# OFD 文件
# --------------------------------------------------------------------------
def write_ofd(path: str, version: str, file_type: str, fields: list[dict],
              rows: int, row_count_length: int) -> None:
    lines: list[bytes] = []
    lines.append(b"OFDCFDAT")
    lines.append(version.ljust(8).encode("ascii"))
    lines.append(b"DEMO0001".ljust(25, b" "))
    lines.append(b"DEMO0002".ljust(26, b" "))
    lines.append(b"20260101")
    lines.append(str(rows).zfill(row_count_length).encode("ascii"))
    lines.append(file_type.ljust(8).encode("ascii"))
    lines.append(b" " * 8)
    lines.append(b" " * 8)
    lines.append(str(len(fields)).zfill(8).encode("ascii"))
    for field in fields:
        lines.append(field["name_en"].encode("ascii"))

    expected_length = sum(field["length"] for field in fields)
    for row in range(rows):
        data = build_row(fields, row)
        assert len(data) == expected_length, (len(data), expected_length)
        lines.append(data)
    lines.append(b"OFDCFEND")

    with open(path, "wb") as handle:
        handle.write(b"\r\n".join(lines) + b"\r\n")


# --------------------------------------------------------------------------
# 定长文件（理财登 WDEP / WPID）
# --------------------------------------------------------------------------
def write_fixed(path: str, marker: str, version: str, fields: list[dict],
                rows: int, data_row_index: int, row_count_row: int) -> None:
    lines: list[str] = []
    lines.append(marker)
    lines.append(version)
    lines.append(str(len(fields)))

    detail_row = 4
    while len(lines) + 1 < row_count_row:
        index = len(lines) - 2  # 1 开始的字段序号
        if 1 <= index <= len(fields):
            field = fields[index - 1]
            lines.append("%d=%s,%d,%d,%s,%s" % (
                field["index"], field["type"], field["length"],
                field["decimals"], field["name_cn"], field["name_en"]))
        else:
            lines.append("")
    lines.append(str(rows))  # 记录数行

    while len(lines) + 1 < data_row_index:
        lines.append("")

    for row in range(rows):
        data = build_row(fields, row)
        lines.append(data.decode(ENCODING))

    lines.append(marker + "END")
    with open(path, "wb") as handle:
        handle.write(("\r\n".join(lines) + "\r\n").encode(ENCODING))
    _ = detail_row


# --------------------------------------------------------------------------
def find_section(ini: Ini, key: str, version: str | None = None) -> str:
    for name in ini.order:
        if key not in name:
            continue
        if version is not None and not name.endswith("|" + version):
            continue
        if name in ("AuthorInformation", "TipDictionary", "Dictionary"):
            continue
        return name
    raise KeyError(f"{ini.path} 中找不到包含 {key} 的配置段")


def main() -> int:
    os.makedirs(DEMO, exist_ok=True)

    jobs = [
        ("OFD_20.ini", "01_001", "20", "OFD_2.0_01_账户类申请_demo.txt", 2),
        ("OFD_21.ini", "02_001_FUND", "21", "OFD_2.1_02_账户类确认_demo.txt", 2),
        ("OFD_22.ini", "01_001_FUND", "22", "OFD_2.2_01_账户类申请_demo.txt", 3),
        ("OFD_22.ini", "04_001_FUND", "22", "OFD_2.2_04_交易类确认_demo.txt", 3),
    ]

    for config_name, key, version, output_name, rows in jobs:
        ini = Ini(os.path.join(CONF, config_name))
        section = find_section(ini, key)
        fields = parse_fields(ini, section)
        row_count_length = int(ini.value(section, "ROWCOUNTLENGTH") or "8")
        file_type = section.split("_")[0]
        write_ofd(os.path.join(DEMO, output_name), version, file_type,
                  fields, rows, row_count_length)
        print("生成", os.path.join("conf", "demo", output_name),
              f"({len(fields)} 字段, {rows} 行)")

    wdep_name = "FIXED_WDEP_理财产品中央数据交换协议_V1.1.ini"
    wdep = Ini(os.path.join(CONF, wdep_name))
    wdep_section = find_section(wdep, "-001-", "1.1")
    wdep_fields = parse_fields(wdep, wdep_section)
    wdep_output = "DEMO000001-DEMO000002-001-20260101-demo.txt"
    write_fixed(os.path.join(DEMO, wdep_output), "WDEP", "1.1", wdep_fields,
                rows=3,
                data_row_index=int(wdep.value(wdep_section, "datarowbeginindex")),
                row_count_row=int(wdep.value(wdep_section, "rowcountcheckrow")))
    print("生成", os.path.join("conf", "demo", wdep_output),
          f"({len(wdep_fields)} 字段, 3 行)")

    wpid_name = "FIXED_WPID_理财登信息披露.ini"
    wpid = Ini(os.path.join(CONF, wpid_name))
    wpid_section = find_section(wpid, "-ID1-", "1.1")
    wpid_fields = parse_fields(wpid, wpid_section)
    wpid_output = "DEMO000001-DEMO000002-ID1-20260101-demo.txt"
    write_fixed(os.path.join(DEMO, wpid_output), "WPID", "1.1", wpid_fields,
                rows=2,
                data_row_index=int(wpid.value(wpid_section, "datarowbeginindex")),
                row_count_row=int(wpid.value(wpid_section, "rowcountcheckrow")))
    print("生成", os.path.join("conf", "demo", wpid_output),
          f"({len(wpid_fields)} 字段, 2 行)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
