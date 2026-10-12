import dataclasses
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import make_secure_app_package as package


class SegmentEncryptionTests(unittest.TestCase):
    def setUp(self):
        self.key = bytes(range(16))
        self.base = 0x60100250
        lengths = [16, 17, 4, 32, 19, 31, 33, 0, 12]
        destinations = [0x0011A000, 0x00118000, 0, 0x10000100, 0x10000200,
                        0x64000100, 0x64000000, 0x30160000, 0x30150000]
        payload = bytearray()
        entries = bytearray()
        offset = package.APP_MANIFEST_HEADER_SIZE + len(lengths) * package.APP_MANIFEST_ENTRY_SIZE
        for segment_id, (size, destination) in enumerate(zip(lengths, destinations)):
            flags = package.SEGMENT_FLAG_ENABLE if segment_id != 8 else 0
            entries.extend(struct.pack('<4I', self.base + offset + len(payload), destination, size, flags))
            payload.extend(bytes([segment_id + 1]) * size)
        self.app = struct.pack('<4I', package.APP_MANIFEST_MAGIC, len(lengths), destinations[0], 0) + entries + payload
        self.legacy = package.parse_legacy_manifest(self.app, 0)

    def build(self, selected=None, encrypt_all=False):
        key = self.key if encrypt_all or selected is not None else None
        manifest, offset, payload = package.make_secure_manifest(
            self.app, self.legacy, self.base, 0, 1, 0, 16, selected, key, encrypt_all=encrypt_all)
        body = manifest + b'\xff' * (offset - len(manifest)) + payload
        package.validate_generated_body(body, self.app, key)
        return body, package.parse_secure_manifest(body)

    def test_plaintext_and_single_segment_compatibility(self):
        for selected in (None, 0, 5, 6):
            with self.subTest(selected=selected):
                body, manifest = self.build(selected)
                encrypted = [entry.segment_id for entry in manifest.entries if entry.flags & package.SEGMENT_FLAG_ENCRYPTED]
                self.assertEqual(encrypted, [] if selected is None else [selected])
                self.assertEqual(len(body) % 16, 0)

    def test_all_excludes_zero_length_and_disabled_segments(self):
        body, manifest = self.build(encrypt_all=True)
        self.assertEqual([entry.segment_id for entry in manifest.entries], list(range(7)))
        self.assertTrue(all(entry.flags & package.SEGMENT_FLAG_ENCRYPTED for entry in manifest.entries))
        self.assertEqual(len({entry.aes_cbc_iv for entry in manifest.entries}), 7)
        for entry in manifest.entries:
            payload = body[entry.src_offset:entry.src_offset + entry.cipher_size]
            full = package.decrypt_aes_128_cbc(payload, self.key, entry.aes_cbc_iv)
            trailing = entry.file_size % 16
            if trailing:
                prefix = entry.cipher_size - 16
                direct = package.decrypt_aes_128_cbc(payload[:prefix], self.key, entry.aes_cbc_iv) if prefix else b''
                tail_iv = payload[prefix - 16:prefix] if prefix else entry.aes_cbc_iv
                tail = package.decrypt_aes_128_cbc(payload[prefix:], self.key, tail_iv)
                self.assertEqual(direct + tail[:trailing], full[:entry.file_size])
                self.assertFalse(any(full[entry.file_size:]))

    def test_iv_generation_retries_zero_and_duplicate(self):
        candidates = [bytes(16), bytes([1]) * 16, bytes([1]) * 16]
        candidates.extend(bytes([value]) * 16 for value in range(2, 8))
        with patch.object(package.os, 'urandom', side_effect=candidates):
            _, manifest = self.build(encrypt_all=True)
        self.assertEqual([entry.aes_cbc_iv for entry in manifest.entries],
                         [bytes([value]) * 16 for value in range(1, 8)])

    def test_inspection_rejects_duplicate_iv_and_unknown_id(self):
        body, manifest = self.build(encrypt_all=True)
        duplicate = dataclasses.replace(manifest.entries[1], aes_cbc_iv=manifest.entries[0].aes_cbc_iv)
        unknown = dataclasses.replace(manifest.entries[1], segment_id=99)
        unaligned = dataclasses.replace(manifest.entries[1], dst_addr=0x00118001)
        for invalid in (duplicate, unknown, unaligned):
            with self.subTest(invalid=invalid):
                entries = [manifest.entries[0], invalid, *manifest.entries[2:]]
                with self.assertRaises(ValueError):
                    package.validate_secure_manifest_layout(body, dataclasses.replace(manifest, entries=entries))

    def test_invalid_selections_and_missing_key(self):
        for selected, key, encrypt_all in ((6, self.key, True), (None, None, True),
                                           (7, self.key, False), (8, self.key, False)):
            with self.subTest(selected=selected, encrypt_all=encrypt_all):
                with self.assertRaises(ValueError):
                    package.make_secure_manifest(self.app, self.legacy, self.base, 0, 1, 0, 16,
                                                 selected, key, encrypt_all=encrypt_all)

    def test_cli_generation_inspection_and_option_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            app_path, key_path = folder / 'app.bin', folder / 'key.bin'
            body_path, manifest_path = folder / 'body.bin', folder / 'manifest.bin'
            app_path.write_bytes(self.app)
            key_path.write_bytes(self.key)
            command = [sys.executable, str(Path(package.__file__).resolve())]
            generate = ['--app-bin', str(app_path), '--manifest-out', str(manifest_path),
                        '--body-out', str(body_path), '--encrypt-all', '--aes-key', str(key_path)]
            result = subprocess.run(command + generate, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            package.validate_generated_body(body_path.read_bytes(), self.app, self.key)
            result = subprocess.run(command + ['--inspect-body', str(body_path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for invalid in (generate + ['--encrypt-segment', 'SYSTEM_DATA'],
                            generate + ['--scheme', 'overall-app'],
                            ['--inspect-body', str(body_path), '--encrypt-all'],
                            ['--inspect-body', str(body_path), '--encrypt-segment', '0']):
                result = subprocess.run(command + invalid, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()