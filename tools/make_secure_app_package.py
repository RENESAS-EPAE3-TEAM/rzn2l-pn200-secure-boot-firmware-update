#!/usr/bin/env python3
"""Create a PN2.0 secure App manifest, package body, and signed package.

The script consumes the existing PN2.0 App raw binary whose first bytes contain
the legacy RZAP app_manifest_t. It converts that manifest into a secure manifest
using package-relative source offsets and per-segment SHA-256 values.

The package body is the contiguous image authenticated by the Renesas Code
Certificate:

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


def resolve_body_out(body_out: Path | None, package_out: Path | None) -> Path | None:
    if body_out and package_out and body_out != package_out:
        raise ValueError("--body-out and legacy --package-out were both provided with different paths")
    return body_out or package_out


def main() -> int:
    parser = argparse.ArgumentParser(description="Create PN2.0 secure App manifest/body/signed package")
    parser.add_argument("--app-bin", required=True, type=Path, help="Input PN2.0 App raw binary")
    parser.add_argument("--manifest-out", required=True, type=Path, help="Output secure manifest binary")
    parser.add_argument("--body-out", type=Path, help="Optional output package body: secure manifest + padding + App raw binary")
    parser.add_argument("--package-out", type=Path, help="Legacy alias for --body-out")
    parser.add_argument("--key-cert", type=Path, help="Input Key Certificate generated by the Renesas signing flow")
    parser.add_argument("--code-cert", type=Path, help="Input Code Certificate generated by the Renesas signing flow")
    parser.add_argument("--signed-package-out", type=Path, help="Output signed package: Key Cert + Code Cert + package body")
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

    body_out = resolve_body_out(args.body_out, args.package_out)
    cert_inputs = [args.key_cert, args.code_cert, args.signed_package_out]
    if any(cert_inputs) and not all(cert_inputs):
        raise ValueError("--key-cert, --code-cert, and --signed-package-out must be provided together")

    app_image = args.app_bin.read_bytes()
    legacy_manifest = parse_legacy_manifest(app_image, args.app_manifest_offset)
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
