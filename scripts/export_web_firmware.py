"""PlatformIO post-build action: package exactly the selected board's images.

The ESP32 bootloader header must be patched by esptool, as with PlatformIO's
normal USB upload. Offsets/mode/frequency/size come from that build environment,
not an ESP32 address table pasted into the website. Does not access USB.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

Import("env")  # noqa: F821 - supplied by PlatformIO/SCons


def export_image(source, target, env):
    profile = env.subst("$PIOENV")
    if profile not in ("esp32-s3", "nodemcu-i2c"):
        return
    build = Path(env.subst("$BUILD_DIR"))
    output = build / "webflash"
    output.mkdir(exist_ok=True)
    application = build / (env.subst("$PROGNAME") + ".bin")
    image = output / "firmware.bin"
    board = env.BoardConfig()
    chip = board.get("build.mcu")
    if profile == "esp32-s3":
        if chip != "esp32s3":
            raise RuntimeError("Installer profile requires ESP32-S3")
        extra = [(env.subst(str(offset)), Path(env.subst(str(path))))
                 for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
        if len(extra) < 3:
            raise RuntimeError("Missing bootloader, partition table or boot_app0")
        parts = extra + [(env.subst("$ESP32_APP_OFFSET"), application)]
        mode = env.subst("${__get_board_flash_mode(__env__)}")
        frequency = env.subst("${__get_board_f_image(__env__)}")
        size = board.get("upload.flash_size")
        if mode not in ("dio", "dout", "qio", "qout") or not size:
            raise RuntimeError("Unresolved flash settings")
        command = [env.subst("$PYTHONEXE"), env.subst("$UPLOADER"),
                   "--chip", chip, "merge_bin", "-o", str(image),
                   "--flash_mode", mode, "--flash_freq", frequency, "--flash_size", size]
        for offset, path in sorted(parts, key=lambda part: int(part[0], 0)):
            if not path.is_file():
                raise RuntimeError(f"Missing flash image: {path}")
            command.extend([offset, str(path)])
        subprocess.run(command, check=True)
    else:
        if chip != "esp8266":
            raise RuntimeError("Installer profile requires ESP8266")
        parts = [("0x0", application)]
        shutil.copyfile(application, image)
    payload = image.read_bytes()
    if not payload or payload[0] != 0xE9:
        raise RuntimeError("Expected bootable Espressif image at offset zero")
    root = Path(env.subst("$PROJECT_DIR")).parent.parent
    metadata = {
        "id": profile, "version": (root / "VERSION").read_text().strip(),
        "sourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "chipFamily": "ESP32-S3" if chip == "esp32s3" else "ESP8266",
        "hardwareTested": False, "offset": 0,
        "sha256": hashlib.sha256(payload).hexdigest(), "bytes": len(payload),
        "sourceParts": [{"offset": int(offset, 0), "name": path.name} for offset, path in parts],
    }
    (output / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Web installer image: {image}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_image)
