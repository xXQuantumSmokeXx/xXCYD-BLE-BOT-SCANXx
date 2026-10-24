"""PlatformIO post-build script: merge bootloader + partitions + app into one flashable .bin"""
import os
import sys

# Windows consoles often run cp1252. A stray non-ASCII character in a print()
# used to abort this step with UnicodeEncodeError, which left a stale .bin in the
# project root. Force UTF-8 on stdout and keep every message ASCII.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

Import("env")


def merge_bin_callback(source, target, env):
    chip = env.get("BOARD_MCU", "esp32")
    flash_size = env.get("BOARD_BUILD_FLASH_SIZE", "4MB")
    flash_mode = env.get("BOARD_FLASH_MODE", "dio")

    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    env_name = env.subst("$PIOENV")
    core_dir = env.subst("$PROJECT_CORE_DIR")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    # 0xe000 holds the OTA-data initialiser on ESP32. The old script omitted it,
    # so the merged image left that sector erased.
    boot_app0 = os.path.join(
        core_dir, "packages", "framework-arduinoespressif32",
        "tools", "partitions", "boot_app0.bin"
    )

    # User-friendly output names matching README documentation
    NAME_MAP = {
        "cyd_ble_bot_scan": "CYD-BLE-BOT-SCAN-1usb",
        "cyd_ble_bot_scan_2usb": "CYD-BLE-BOT-SCAN-2usb",
    }
    base_name = NAME_MAP.get(env_name, env_name)
    merged = os.path.join(project_dir, f"{base_name}.bin")

    for path, label in ((bootloader, "bootloader"), (partitions, "partitions"), (firmware, "firmware")):
        if not os.path.isfile(path):
            print(f"merge_bin: SKIP - {label} not found at {path}")
            return

    # esptool is installed alongside PlatformIO
    esptool = os.path.join(core_dir, "packages", "tool-esptoolpy", "esptool.py")
    if not os.path.isfile(esptool):
        esptool = "esptool.py"

    parts = [f'0x1000 "{bootloader}"', f'0x8000 "{partitions}"']
    if os.path.isfile(boot_app0):
        parts.append(f'0xe000 "{boot_app0}"')
    else:
        print("merge_bin: NOTE - boot_app0.bin not found, 0xe000 will be omitted")
    parts.append(f'0x10000 "{firmware}"')

    cmd = (
        f'python "{esptool}" --chip {chip} merge_bin '
        f'-o "{merged}" '
        f'--flash_mode {flash_mode} '
        f'--flash_size {flash_size} '
        + " ".join(parts)
    )
    print(f"merge_bin: {cmd}")
    ret = os.system(cmd)
    if ret == 0 and os.path.isfile(merged):
        size_mb = os.path.getsize(merged) / (1024 * 1024)
        print(f"merge_bin: OK -> {merged} ({size_mb:.2f} MB)")
        print(f"merge_bin: flash {base_name}.bin from the SD card")
    else:
        print(f"merge_bin: FAILED (exit code {ret}) - project-root .bin may be stale, do not flash it")


# Hook into the firmware.bin target rather than buildprog -
# on ESP32, buildprog may fire before firmware.bin is generated
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_callback)