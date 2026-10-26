#!/usr/bin/env python3
"""Read chip identity and security flags only. Never reads keys or changes eFuses."""
import argparse
import contextlib
import io
import json
import sys

SUPPORTED = {"ESP32": "esp32", "ESP32-S3": "esp32s3"}
MARKER = "TMBOX_DEVICE_JSON:"


def inspect_device(esp):
    if esp.CHIP_NAME not in SUPPORTED:
        raise ValueError("Endast klassisk ESP32 och ESP32-S3 stöds av detta verktyg.")
    secure_download = getattr(esp, "secure_download_mode", None)
    if type(secure_download) is not bool:
        raise ValueError("Säkerhetsläget kunde inte fastställas.")
    if secure_download:
        raise ValueError("Secure Download Mode stöds inte.")
    # These pinned esptool methods read eFuse registers. No write/burn API is used.
    encryption_value = esp.get_flash_encryption_enabled()
    boot_value = esp.get_secure_boot_enabled()
    if type(encryption_value) is not bool or type(boot_value) not in (int, bool) or boot_value < 0:
        raise ValueError("Säkerhetsstatus kunde inte tolkas.")
    encrypted = encryption_value
    secure_boot = bool(boot_value)
    if encrypted or secure_boot:
        raise ValueError("Flashkryptering eller Secure Boot är aktiverat. Detta verktyg får inte användas.")
    mac = esp.read_mac()
    if (not isinstance(mac, tuple) or len(mac) != 6 or
            any(type(x) is not int or not 0 <= x <= 255 for x in mac) or
            not any(mac) or all(x == 255 for x in mac) or mac[0] & 1):
        raise ValueError("En giltig fysisk MAC-adress kunde inte läsas.")
    return {
        "schema": 1, "chip": esp.CHIP_NAME, "esptoolChip": SUPPORTED[esp.CHIP_NAME],
        "mac": ":".join("%02x" % x for x in mac),
        "security": {"flashEncryption": False, "secureBoot": False, "secureDownload": False},
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description="Read-only ESP32 identity/security probe.")
    parser.add_argument("--port", required=True)
    args = parser.parse_args(argv)
    esp = None
    try:
        import esptool
        if esptool.__version__ != "4.11.0":
            raise ValueError("Fel esptool-version.")
        with contextlib.redirect_stdout(io.StringIO()):
            esp = esptool.detect_chip(port=args.port, baud=115200, connect_mode="default_reset")
            info = inspect_device(esp)
        print(MARKER + json.dumps(info))
        return 0
    except Exception as error:
        print("Chip-/säkerhetskontrollen misslyckades: " + str(error), file=sys.stderr)
        return 1
    finally:
        if esp is not None:
            esp._port.close()


if __name__ == "__main__":
    raise SystemExit(main())
