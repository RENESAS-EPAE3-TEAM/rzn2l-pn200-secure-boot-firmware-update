#!/usr/bin/env python3
"""Create a PN2.0 secure App body and signed package.

The script consumes the existing PN2.0 App raw binary whose first bytes contain
the legacy RZAP app_manifest_t.

The package body is the contiguous image authenticated by the Renesas Code
Certificate. The default plan-B layout signs the whole App without encryption:

    PN2.0 App raw binary

The retained RZSM mode converts the legacy manifest into a secure manifest using
package-relative source offsets and per-segment SHA-256 values:

    Secure Manifest + padding + PN2.0 App raw binary

The signed package is the complete flash/update object consumed by the SSBL:

    Key Certificate + Code Certificate + package body
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

APP_MANIFEST_MAGIC = 0x50415A52  # 'RZAP'
APP_MANIFEST_ENTRY_SIZE = 16
APP_MANIFEST_HEADER_SIZE = 16
APP_MANIFEST_MAX_ENTRIES = 9

SECURE_MANIFEST_MAGIC = 0x4D535A52  # 'RZSM'
SECURE_MANIFEST_VERSION = 0x00010000
SECURE_MANIFEST_HEADER_SIZE = 64
SECURE_MANIFEST_ENTRY_SIZE = 64

KEY_CERT_SIZE = 0xE0
CODE_CERT_SIZE = 0x120
TOTAL_CERT_SIZE = KEY_CERT_SIZE + CODE_CERT_SIZE

SEGMENT_FLAG_ENABLE = 1 << 0
SEGMENT_FLAG_HASH_VALID = 1 << 3

SEGMENT_NAMES = [
    "LDR_PRG",
    "LDR_DATA",
    "VECTOR",
    "USER_PRG",
    "USER_DATA",
    "SYSTEM_PRG",
    "SYSTEM_DATA",
    "NONCACHE",
    "SHARED_NONCACHE_BUFFER",
]


@dataclass(frozen=True)
class LegacyEntry:
    segment_id: int
    src: int
    dst: int
    size: int
    flags: int


@dataclass(frozen=True)
class LegacyManifest:
    entry_point: int
    entries: list[LegacyEntry]


@dataclass(frozen=True)
class CodeCertHeader:
    magic_num: int
    manifest_ver: int
    flags: int
    write_addr: int
    dest_addr: int
    img_size: int
    img_ver: int
    opts_info: int


@dataclass(frozen=True)
class SecureEntry:
    segment_id: int
    flags: int
    src_offset: int
    dst_addr: int
    file_size: int
    mem_size: int
    load_attr: int
    reserved: int
    sha256_words: tuple[int, ...]


@dataclass(frozen=True)
class SecureManifest:
    magic: int
    format_version: int
    header_size: int
    manifest_size: int
    package_version: int
    package_flags: int
    package_size: int
    payload_offset: int
    payload_size: int
    segment_count: int
    entry_point: int
    image_base_addr: int
    app_manifest_offset: int
    signed_region_offset: int
    signed_region_size: int
    header_crc32: int
    entries: list[SecureEntry]


def parse_int(value: str) -> int:
    return int(value, 0)


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        raise ValueError("alignment must be positive")
    return (value + alignment - 1) // alignment * alignment


def hex32(value: int) -> str:
    return f"0x{value:08X}"


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_fixed_size(path: Path, expected_size: int, label: str) -> bytes:
    data = path.read_bytes()
    if len(data) != expected_size:
        raise ValueError(f"{label} must be {expected_size} bytes, got {len(data)}: {path}")
    return data


def parse_code_cert_header(code_cert: bytes) -> CodeCertHeader:
    if len(code_cert) < 32:
        raise ValueError("Code Certificate is too small to contain a header")
    return CodeCertHeader(*struct.unpack_from("<IIIIIIII", code_cert, 0))


def validate_code_cert_layout(code_cert_header: CodeCertHeader, body_addr: int, body_size: int) -> None:
    if code_cert_header.dest_addr != body_addr:
        raise ValueError(
            "Code Certificate dest_addr mismatch: "
            f"expected {hex32(body_addr)}, got {hex32(code_cert_header.dest_addr)}"
        )
    if code_cert_header.img_size != body_size:
        raise ValueError(
            "Code Certificate img_size mismatch: "
            f"expected 0x{body_size:X}, got 0x{code_cert_header.img_size:X}"
        )


def parse_legacy_manifest(app_image: bytes, app_manifest_offset: int) -> LegacyManifest:
    header_end = app_manifest_offset + APP_MANIFEST_HEADER_SIZE
    if header_end > len(app_image):
        raise ValueError("legacy app manifest header is outside the App image")

    magic, entry_count, entry_point, _reserved = struct.unpack_from("<IIII", app_image, app_manifest_offset)
    if magic != APP_MANIFEST_MAGIC:
        raise ValueError(f"legacy app manifest magic mismatch: 0x{magic:08X}")
    if entry_count > APP_MANIFEST_MAX_ENTRIES:
        raise ValueError(f"legacy app manifest entry_count is too large: {entry_count}")

    entries: list[LegacyEntry] = []
    entry_offset = header_end
    for segment_id in range(entry_count):
        entry_end = entry_offset + APP_MANIFEST_ENTRY_SIZE
        if entry_end > len(app_image):
            raise ValueError("legacy app manifest entry table is outside the App image")
        src, dst, size, flags = struct.unpack_from("<IIII", app_image, entry_offset)
        entries.append(LegacyEntry(segment_id, src, dst, size, flags))
        entry_offset = entry_end

    return LegacyManifest(entry_point, entries)


def make_secure_manifest(
    app_image: bytes,
    legacy_manifest: LegacyManifest,
    image_base: int,
    app_manifest_offset: int,
    package_version: int,
    package_flags: int,
    alignment: int,
) -> tuple[bytes, int]:
    active_entries = [entry for entry in legacy_manifest.entries if (entry.flags & SEGMENT_FLAG_ENABLE) and entry.size]
    manifest_size = SECURE_MANIFEST_HEADER_SIZE + len(active_entries) * SECURE_MANIFEST_ENTRY_SIZE
    payload_offset = align_up(manifest_size, alignment)
    payload_size = len(app_image)
    package_size = payload_offset + payload_size

    secure_entries = bytearray()
    for entry in active_entries:
        image_offset = entry.src - image_base
        if image_offset < 0:
            raise ValueError(
                f"segment {entry.segment_id} source 0x{entry.src:08X} is below image base 0x{image_base:08X}"
            )
        if image_offset + entry.size > len(app_image):
            raise ValueError(
                f"segment {entry.segment_id} source range 0x{entry.src:08X}+0x{entry.size:X} is outside the App image"
            )

        segment_data = app_image[image_offset:image_offset + entry.size]
        digest_words = struct.unpack("<8I", hashlib.sha256(segment_data).digest())
        flags = SEGMENT_FLAG_ENABLE | SEGMENT_FLAG_HASH_VALID
        src_offset = payload_offset + image_offset

        secure_entries += struct.pack(
            "<IIIIIIII8I",
            entry.segment_id,
            flags,
            src_offset,
            entry.dst,
            entry.size,
            entry.size,
            0,
            0,
            *digest_words,
        )

    header_without_crc = struct.pack(
        "<IIIIIIIIIIIIIIII",
        SECURE_MANIFEST_MAGIC,
        SECURE_MANIFEST_VERSION,
        SECURE_MANIFEST_HEADER_SIZE,
        manifest_size,
        package_version,
        package_flags,
        package_size,
        payload_offset,
        payload_size,
        len(active_entries),
        legacy_manifest.entry_point,
        image_base,
        app_manifest_offset,
        0,
        package_size,
        0,
    )

    crc = zlib.crc32(header_without_crc + secure_entries) & 0xFFFFFFFF
    header = header_without_crc[:-4] + struct.pack("<I", crc)
    return header + bytes(secure_entries), payload_offset


def parse_secure_manifest(body: bytes) -> SecureManifest:
    if len(body) < SECURE_MANIFEST_HEADER_SIZE:
        raise ValueError("package body is too small to contain a secure manifest header")

    header_values = struct.unpack_from("<IIIIIIIIIIIIIIII", body, 0)
    (
        magic,
        format_version,
        header_size,
        manifest_size,
        package_version,
        package_flags,
        package_size,
        payload_offset,
        payload_size,
        segment_count,
        entry_point,
        image_base_addr,
        app_manifest_offset,
        signed_region_offset,
        signed_region_size,
        header_crc32,
    ) = header_values

    if magic != SECURE_MANIFEST_MAGIC:
        raise ValueError(f"secure manifest magic mismatch: 0x{magic:08X}")
    if format_version != SECURE_MANIFEST_VERSION:
        raise ValueError(f"secure manifest version mismatch: 0x{format_version:08X}")
    if header_size != SECURE_MANIFEST_HEADER_SIZE:
        raise ValueError(f"secure manifest header_size mismatch: {header_size}")
    if segment_count > APP_MANIFEST_MAX_ENTRIES:
        raise ValueError(f"secure manifest segment_count is too large for PN2.0 ABI: {segment_count}")

    expected_manifest_size = SECURE_MANIFEST_HEADER_SIZE + segment_count * SECURE_MANIFEST_ENTRY_SIZE
    if manifest_size != expected_manifest_size:
        raise ValueError(f"secure manifest_size mismatch: expected {expected_manifest_size}, got {manifest_size}")
    if manifest_size > len(body):
        raise ValueError("secure manifest extends outside the package body")
    if package_size != len(body):
        raise ValueError(f"package_size mismatch: expected {len(body)}, got {package_size}")
    if payload_offset < manifest_size or payload_offset > package_size:
        raise ValueError("payload_offset is outside the package body")
    if payload_size != package_size - payload_offset:
        raise ValueError("payload_size does not match package_size - payload_offset")
    if signed_region_offset != 0 or signed_region_size != package_size:
        raise ValueError("signed region does not cover the whole package body")

    crc_input = bytearray(body[:manifest_size])
    struct.pack_into("<I", crc_input, SECURE_MANIFEST_HEADER_SIZE - 4, 0)
    calculated_crc = zlib.crc32(crc_input) & 0xFFFFFFFF
    if calculated_crc != header_crc32:
        raise ValueError(f"secure manifest crc mismatch: expected 0x{header_crc32:08X}, got 0x{calculated_crc:08X}")

    entries: list[SecureEntry] = []
    entry_offset = SECURE_MANIFEST_HEADER_SIZE
    for _ in range(segment_count):
        values = struct.unpack_from("<IIIIIIII8I", body, entry_offset)
        entries.append(SecureEntry(*values[:8], tuple(values[8:])))
        entry_offset += SECURE_MANIFEST_ENTRY_SIZE

    return SecureManifest(*header_values, entries)


def validate_secure_manifest_against_legacy(body: bytes, manifest: SecureManifest) -> LegacyManifest:
    legacy_offset = manifest.payload_offset + manifest.app_manifest_offset
    legacy_manifest = parse_legacy_manifest(body, legacy_offset)
    active_entries = [entry for entry in legacy_manifest.entries if (entry.flags & SEGMENT_FLAG_ENABLE) and entry.size]

    if legacy_manifest.entry_point != manifest.entry_point:
        raise ValueError(
            "legacy entry_point mismatch: "
            f"expected {hex32(manifest.entry_point)}, got {hex32(legacy_manifest.entry_point)}"
        )
    if len(active_entries) != manifest.segment_count:
        raise ValueError(
            "legacy active segment count mismatch: "
            f"expected {manifest.segment_count}, got {len(active_entries)}"
        )

    secure_entries = {entry.segment_id: entry for entry in manifest.entries if entry.flags & SEGMENT_FLAG_ENABLE}
    for legacy_entry in active_entries:
        secure_entry = secure_entries.get(legacy_entry.segment_id)
        if secure_entry is None:
            raise ValueError(f"secure manifest is missing segment {legacy_entry.segment_id}")

        image_offset = legacy_entry.src - manifest.image_base_addr
        expected_src_offset = manifest.payload_offset + image_offset
        if legacy_entry.src < manifest.image_base_addr:
            raise ValueError(f"legacy segment {legacy_entry.segment_id} source is below image_base")
        if expected_src_offset + legacy_entry.size > manifest.package_size:
            raise ValueError(f"legacy segment {legacy_entry.segment_id} source range is outside package body")

        if secure_entry.src_offset != expected_src_offset:
            raise ValueError(f"segment {legacy_entry.segment_id} src_offset mismatch")
        if secure_entry.dst_addr != legacy_entry.dst:
            raise ValueError(f"segment {legacy_entry.segment_id} dst_addr mismatch")
        if secure_entry.file_size != legacy_entry.size or secure_entry.mem_size != legacy_entry.size:
            raise ValueError(f"segment {legacy_entry.segment_id} size mismatch")

        payload = body[secure_entry.src_offset:secure_entry.src_offset + secure_entry.file_size]
        digest_words = struct.unpack("<8I", hashlib.sha256(payload).digest())
        if secure_entry.sha256_words != digest_words:
            raise ValueError(f"segment {legacy_entry.segment_id} sha256 mismatch")

    return legacy_manifest


def inspect_package_body(body: bytes, package_base: int, code_cert: bytes | None, summary_out: Path | None) -> None:
    manifest = parse_secure_manifest(body)
    legacy_manifest = validate_secure_manifest_against_legacy(body, manifest)
    body_addr = package_base + TOTAL_CERT_SIZE

    if code_cert is not None:
        validate_code_cert_layout(parse_code_cert_header(code_cert), body_addr, len(body))

    summary = {
        "manifest_magic": "RZSM",
        "package_size": manifest.package_size,
        "payload_offset": manifest.payload_offset,
        "payload_size": manifest.payload_size,
        "entry_point": hex32(manifest.entry_point),
        "image_base": hex32(manifest.image_base_addr),
        "package_base": hex32(package_base),
        "body_addr": hex32(body_addr),
        "body_sha256": sha256_hex(body),
        "segments": [
            {
                "segment_id": entry.segment_id,
                "name": SEGMENT_NAMES[entry.segment_id] if entry.segment_id < len(SEGMENT_NAMES) else f"SEGMENT_{entry.segment_id}",
                "src_offset_in_body": hex32(entry.src_offset),
                "dst": hex32(entry.dst_addr),
                "file_size": entry.file_size,
                "mem_size": entry.mem_size,
                "flags": hex32(entry.flags),
            }
            for entry in manifest.entries
        ],
    }

    if summary_out:
        summary_out.parent.mkdir(parents=True, exist_ok=True)
        summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(f"inspect ok:       RZSM package body ({len(body)} bytes)")
    print(f"body address:     {hex32(body_addr)}")
    print(f"segments:         {manifest.segment_count}")
    print(f"entry point:      {hex32(legacy_manifest.entry_point)}")
    if summary_out:
        print(f"summary:          {summary_out}")


def legacy_manifest_size(legacy_manifest: LegacyManifest) -> int:
    return APP_MANIFEST_HEADER_SIZE + len(legacy_manifest.entries) * APP_MANIFEST_ENTRY_SIZE


def inspect_overall_app_body(body: bytes, package_base: int, image_base: int, code_cert: bytes | None, summary_out: Path | None) -> None:
    legacy_manifest = parse_legacy_manifest(body, 0)
    body_addr = package_base + TOTAL_CERT_SIZE

    if code_cert is not None:
        validate_code_cert_layout(parse_code_cert_header(code_cert), body_addr, len(body))

    summary = {
        "scheme": "overall-app",
        "manifest_magic": "RZAP",
        "package_base": hex32(package_base),
        "body_addr": hex32(body_addr),
        "body_size": len(body),
        "body_sha256": sha256_hex(body),
        "image_base": hex32(image_base),
        "entry_point": hex32(legacy_manifest.entry_point),
        "segments": [
            {
                "segment_id": entry.segment_id,
                "name": SEGMENT_NAMES[entry.segment_id] if entry.segment_id < len(SEGMENT_NAMES) else f"SEGMENT_{entry.segment_id}",
                "src": hex32(entry.src),
                "runtime_src": hex32(body_addr + (entry.src - image_base)) if entry.src >= image_base else None,
                "dst": hex32(entry.dst),
                "size": entry.size,
                "flags": hex32(entry.flags),
            }
            for entry in legacy_manifest.entries
            if (entry.flags & SEGMENT_FLAG_ENABLE) and entry.size
        ],
    }

    if summary_out:
        summary_out.parent.mkdir(parents=True, exist_ok=True)
        summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(f"inspect ok:       overall App body ({len(body)} bytes)")
    print(f"body address:     {hex32(body_addr)}")
    print(f"segments:         {len(summary['segments'])}")
    print(f"entry point:      {hex32(legacy_manifest.entry_point)}")
    if summary_out:
        print(f"summary:          {summary_out}")


def write_json_summary(
    output_path: Path,
    legacy_manifest: LegacyManifest,
    image_base: int,
    payload_offset: int,
    manifest: bytes,
    app_image: bytes,
    body: bytes,
    package_base: int,
    manifest_out: Path,
    body_out: Path | None,
    signed_package_out: Path | None,
    key_cert: bytes | None,
    code_cert: bytes | None,
) -> None:
    entries = []
    for entry in legacy_manifest.entries:
        if not ((entry.flags & SEGMENT_FLAG_ENABLE) and entry.size):
            continue
        image_offset = entry.src - image_base
        entries.append(
            {
                "segment_id": entry.segment_id,
                "name": SEGMENT_NAMES[entry.segment_id] if entry.segment_id < len(SEGMENT_NAMES) else f"SEGMENT_{entry.segment_id}",
                "src": hex32(entry.src),
                "image_offset": hex32(image_offset),
                "src_offset_in_body": hex32(payload_offset + image_offset),
                "dst": hex32(entry.dst),
                "size": entry.size,
                "flags": hex32(entry.flags),
            }
        )

    body_addr = package_base + TOTAL_CERT_SIZE
    summary: dict[str, object]
    summary = {
        "manifest_magic": "RZSM",
        "manifest_out": str(manifest_out),
        "manifest_size": len(manifest),
        "payload_offset": payload_offset,
        "payload_size": len(app_image),
        "package_size": len(body),
        "body_size": len(body),
        "entry_point": hex32(legacy_manifest.entry_point),
        "image_base": hex32(image_base),
        "app_image_sha256": sha256_hex(app_image),
        "body_sha256": sha256_hex(body),
        "layout": {
            "package_base": hex32(package_base),
            "key_cert_addr": hex32(package_base),
            "key_cert_size": KEY_CERT_SIZE,
            "code_cert_addr": hex32(package_base + KEY_CERT_SIZE),
            "code_cert_size": CODE_CERT_SIZE,
            "body_addr": hex32(body_addr),
            "body_size": len(body),
            "manifest_addr": hex32(body_addr),
            "payload_addr": hex32(body_addr + payload_offset),
            "payload_size": len(app_image),
        },
        "outputs": {
            "body_out": str(body_out) if body_out else None,
            "signed_package_out": str(signed_package_out) if signed_package_out else None,
        },
        "segments": entries,
    }

    if key_cert is not None and code_cert is not None:
        code_cert_header = parse_code_cert_header(code_cert)
        signed_package_size = TOTAL_CERT_SIZE + len(body)
        summary["certificates"] = {
            "key_cert_size": len(key_cert),
            "key_cert_sha256": sha256_hex(key_cert),
            "code_cert_size": len(code_cert),
            "code_cert_sha256": sha256_hex(code_cert),
            "code_cert_header": {
                "magic_num": hex32(code_cert_header.magic_num),
                "manifest_ver": hex32(code_cert_header.manifest_ver),
                "flags": hex32(code_cert_header.flags),
                "write_addr": hex32(code_cert_header.write_addr),
                "dest_addr": hex32(code_cert_header.dest_addr),
                "img_size": code_cert_header.img_size,
                "img_ver": hex32(code_cert_header.img_ver),
                "opts_info": hex32(code_cert_header.opts_info),
            },
        }
        summary["signed_package"] = {
            "size": signed_package_size,
            "sha256": sha256_hex(key_cert + code_cert + body),
        }

    output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def write_overall_app_json_summary(
    output_path: Path,
    legacy_manifest: LegacyManifest,
    image_base: int,
    app_image: bytes,
    package_base: int,
    manifest_out: Path | None,
    body_out: Path | None,
    signed_package_out: Path | None,
    key_cert: bytes | None,
    code_cert: bytes | None,
) -> None:
    body_addr = package_base + TOTAL_CERT_SIZE
    entries = []
    for entry in legacy_manifest.entries:
        if not ((entry.flags & SEGMENT_FLAG_ENABLE) and entry.size):
            continue
        image_offset = entry.src - image_base
        entries.append(
            {
                "segment_id": entry.segment_id,
                "name": SEGMENT_NAMES[entry.segment_id] if entry.segment_id < len(SEGMENT_NAMES) else f"SEGMENT_{entry.segment_id}",
                "src": hex32(entry.src),
                "runtime_src": hex32(body_addr + image_offset) if image_offset >= 0 else None,
                "dst": hex32(entry.dst),
                "size": entry.size,
                "flags": hex32(entry.flags),
            }
        )

    summary: dict[str, object] = {
        "scheme": "overall-app",
        "manifest_magic": "RZAP",
        "manifest_out": str(manifest_out) if manifest_out else None,
        "manifest_size": legacy_manifest_size(legacy_manifest),
        "package_size": len(app_image),
        "body_size": len(app_image),
        "entry_point": hex32(legacy_manifest.entry_point),
        "image_base": hex32(image_base),
        "app_image_sha256": sha256_hex(app_image),
        "body_sha256": sha256_hex(app_image),
        "layout": {
            "package_base": hex32(package_base),
            "key_cert_addr": hex32(package_base),
            "key_cert_size": KEY_CERT_SIZE,
            "code_cert_addr": hex32(package_base + KEY_CERT_SIZE),
            "code_cert_size": CODE_CERT_SIZE,
            "body_addr": hex32(body_addr),
            "body_size": len(app_image),
            "legacy_manifest_addr": hex32(body_addr),
        },
        "outputs": {
            "body_out": str(body_out) if body_out else None,
            "signed_package_out": str(signed_package_out) if signed_package_out else None,
        },
        "segments": entries,
    }

    if key_cert is not None and code_cert is not None:
        code_cert_header = parse_code_cert_header(code_cert)
        signed_package_size = TOTAL_CERT_SIZE + len(app_image)
        summary["certificates"] = {
            "key_cert_size": len(key_cert),
            "key_cert_sha256": sha256_hex(key_cert),
            "code_cert_size": len(code_cert),
            "code_cert_sha256": sha256_hex(code_cert),
            "code_cert_header": {
                "magic_num": hex32(code_cert_header.magic_num),
                "manifest_ver": hex32(code_cert_header.manifest_ver),
                "flags": hex32(code_cert_header.flags),
                "write_addr": hex32(code_cert_header.write_addr),
                "dest_addr": hex32(code_cert_header.dest_addr),
                "img_size": code_cert_header.img_size,
                "img_ver": hex32(code_cert_header.img_ver),
                "opts_info": hex32(code_cert_header.opts_info),
            },
        }
        summary["signed_package"] = {
            "size": signed_package_size,
            "sha256": sha256_hex(key_cert + code_cert + app_image),
        }

    output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def resolve_body_out(body_out: Path | None, package_out: Path | None) -> Path | None:
    if body_out and package_out and body_out != package_out:
        raise ValueError("--body-out and legacy --package-out were both provided with different paths")
    return body_out or package_out


def main() -> int:
    parser = argparse.ArgumentParser(description="Create PN2.0 secure App manifest/body/signed package")
    parser.add_argument("--scheme", choices=("rzsm", "overall-app"), default="rzsm", help="Packaging scheme: rzsm for plan A, overall-app for plan B")
    parser.add_argument("--app-bin", type=Path, help="Input PN2.0 App raw binary")
    parser.add_argument("--manifest-out", type=Path, help="Output manifest binary: RZAP copy for overall-app, RZSM for rzsm")
    parser.add_argument("--body-out", type=Path, help="Optional output package body for the selected scheme")
    parser.add_argument("--package-out", type=Path, help="Legacy alias for --body-out")
    parser.add_argument("--key-cert", type=Path, help="Input Key Certificate generated by the Renesas signing flow")
    parser.add_argument("--code-cert", type=Path, help="Input Code Certificate generated by the Renesas signing flow")
    parser.add_argument("--signed-package-out", type=Path, help="Output signed package: Key Cert + Code Cert + package body")
    parser.add_argument("--inspect-body", type=Path, help="Inspect and validate an existing package body")
    parser.add_argument("--inspect-signed-package", type=Path, help="Inspect and validate an existing signed package")
    parser.add_argument("--summary-out", type=Path, help="Optional JSON summary output")
    parser.add_argument("--image-base", type=parse_int, default=0x60100050, help="Link-time xSPI base address of the App image")
    parser.add_argument("--package-base", type=parse_int, default=0x60100050, help="xSPI base address of the complete signed package")
    parser.add_argument("--app-manifest-offset", type=parse_int, default=0, help="Offset of the legacy RZAP app_manifest_t in the App image")
    parser.add_argument("--package-version", type=parse_int, default=1, help="Monotonic package version for future anti-rollback")
    parser.add_argument("--package-flags", type=parse_int, default=0, help="Reserved package flags")
    parser.add_argument("--align", type=parse_int, default=16, help="Payload alignment in bytes")
    parser.add_argument(
        "--no-cert-layout-check",
        action="store_true",
        help="Do not check Code Certificate dest_addr/img_size against the generated body layout",
    )
    args = parser.parse_args()

    if args.inspect_body and args.inspect_signed_package:
        raise ValueError("--inspect-body and --inspect-signed-package are mutually exclusive")

    if args.inspect_body or args.inspect_signed_package:
        if args.app_bin or args.manifest_out or args.body_out or args.package_out or args.signed_package_out:
            raise ValueError("inspect mode cannot be combined with generation outputs")
        if args.inspect_body:
            body = args.inspect_body.read_bytes()
            code_cert = args.code_cert.read_bytes() if args.code_cert else None
        else:
            signed_package = args.inspect_signed_package.read_bytes()
            if len(signed_package) <= TOTAL_CERT_SIZE:
                raise ValueError("signed package is too small")
            code_cert = signed_package[KEY_CERT_SIZE:TOTAL_CERT_SIZE]
            body = signed_package[TOTAL_CERT_SIZE:]

        if "overall-app" == args.scheme:
            inspect_overall_app_body(body, args.package_base, args.image_base, code_cert, args.summary_out)
        else:
            inspect_package_body(body, args.package_base, code_cert, args.summary_out)
        return 0

    if args.app_bin is None:
        raise ValueError("--app-bin is required in generation mode")
    if ("rzsm" == args.scheme) and (args.manifest_out is None):
        raise ValueError("--manifest-out is required when --scheme rzsm")

    body_out = resolve_body_out(args.body_out, args.package_out)
    cert_inputs = [args.key_cert, args.code_cert, args.signed_package_out]
    if any(cert_inputs) and not all(cert_inputs):
        raise ValueError("--key-cert, --code-cert, and --signed-package-out must be provided together")

    app_image = args.app_bin.read_bytes()
    legacy_manifest = parse_legacy_manifest(app_image, args.app_manifest_offset)

    if "overall-app" == args.scheme:
        body = app_image
        if args.manifest_out:
            manifest_end = args.app_manifest_offset + legacy_manifest_size(legacy_manifest)
            args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
            args.manifest_out.write_bytes(app_image[args.app_manifest_offset:manifest_end])

        if body_out:
            body_out.parent.mkdir(parents=True, exist_ok=True)
            body_out.write_bytes(body)

        key_cert = None
        code_cert = None
        if args.signed_package_out:
            key_cert = read_fixed_size(args.key_cert, KEY_CERT_SIZE, "Key Certificate")
            code_cert = read_fixed_size(args.code_cert, CODE_CERT_SIZE, "Code Certificate")
            code_cert_header = parse_code_cert_header(code_cert)
            if not args.no_cert_layout_check:
                validate_code_cert_layout(code_cert_header, args.package_base + TOTAL_CERT_SIZE, len(body))
            args.signed_package_out.parent.mkdir(parents=True, exist_ok=True)
            args.signed_package_out.write_bytes(key_cert + code_cert + body)

        if args.summary_out:
            args.summary_out.parent.mkdir(parents=True, exist_ok=True)
            write_overall_app_json_summary(
                args.summary_out,
                legacy_manifest,
                args.image_base,
                app_image,
                args.package_base,
                args.manifest_out,
                body_out,
                args.signed_package_out,
                key_cert,
                code_cert,
            )

        if args.manifest_out:
            print(f"legacy manifest:  {args.manifest_out} ({legacy_manifest_size(legacy_manifest)} bytes)")
        if body_out:
            print(f"package body:     {body_out} ({len(body)} bytes)")
        if args.signed_package_out:
            print(f"signed package:   {args.signed_package_out} ({TOTAL_CERT_SIZE + len(body)} bytes)")
            print(f"body address:     0x{args.package_base + TOTAL_CERT_SIZE:08X}")
        print("scheme:           overall-app")
        print(f"segments:         {sum(1 for entry in legacy_manifest.entries if (entry.flags & SEGMENT_FLAG_ENABLE) and entry.size)}")
        print(f"entry point:      0x{legacy_manifest.entry_point:08X}")
        return 0

    manifest, payload_offset = make_secure_manifest(
        app_image,
        legacy_manifest,
        args.image_base,
        args.app_manifest_offset,
        args.package_version,
        args.package_flags,
        args.align,
    )

    args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
    args.manifest_out.write_bytes(manifest)

    padding = bytes([0xFF]) * (payload_offset - len(manifest))
    body = manifest + padding + app_image

    if body_out:
        body_out.parent.mkdir(parents=True, exist_ok=True)
        body_out.write_bytes(body)

    key_cert = None
    code_cert = None
    if args.signed_package_out:
        key_cert = read_fixed_size(args.key_cert, KEY_CERT_SIZE, "Key Certificate")
        code_cert = read_fixed_size(args.code_cert, CODE_CERT_SIZE, "Code Certificate")
        code_cert_header = parse_code_cert_header(code_cert)
        if not args.no_cert_layout_check:
            validate_code_cert_layout(code_cert_header, args.package_base + TOTAL_CERT_SIZE, len(body))
        args.signed_package_out.parent.mkdir(parents=True, exist_ok=True)
        args.signed_package_out.write_bytes(key_cert + code_cert + body)

    if args.summary_out:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        write_json_summary(
            args.summary_out,
            legacy_manifest,
            args.image_base,
            payload_offset,
            manifest,
            app_image,
            body,
            args.package_base,
            args.manifest_out,
            body_out,
            args.signed_package_out,
            key_cert,
            code_cert,
        )

    print(f"secure manifest: {args.manifest_out} ({len(manifest)} bytes)")
    if body_out:
        print(f"package body:    {body_out} ({len(body)} bytes)")
    if args.signed_package_out:
        print(f"signed package:  {args.signed_package_out} ({TOTAL_CERT_SIZE + len(body)} bytes)")
        print(f"body address:    0x{args.package_base + TOTAL_CERT_SIZE:08X}")
    print(f"segments:        {sum(1 for entry in legacy_manifest.entries if (entry.flags & SEGMENT_FLAG_ENABLE) and entry.size)}")
    print(f"entry point:     0x{legacy_manifest.entry_point:08X}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
