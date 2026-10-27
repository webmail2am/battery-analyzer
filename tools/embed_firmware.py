"""Builds the firmware for both Pro Mini boards and embeds the .hex files in
tools/BatteryAnalyzer.html, so the PC app can flash a board without VS Code.

Run after changing the firmware (needs PlatformIO):
    python tools/embed_firmware.py
"""
import datetime
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "tools", "BatteryAnalyzer.html")
ENVS = {  # key used by the app -> PlatformIO environment
    "lgt": "promini_3v3_4mhz",
    "atmega": "promini_3v3",
}


def pio_command():
    exe = shutil.which("pio") or shutil.which("platformio")
    return [exe] if exe else [sys.executable, "-m", "platformio"]


def version():
    try:
        rev = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        rev = "local"
    return datetime.date.today().isoformat() + " " + rev


def main():
    fw = {}
    ver = version()
    for key, env in ENVS.items():
        print("building", env, "...")
        subprocess.run(pio_command() + ["run", "-e", env], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL)
        hex_path = os.path.join(ROOT, ".pio", "build", env, "firmware.hex")
        with open(hex_path, encoding="ascii") as f:
            fw[key] = {"env": env, "version": ver, "hex": f.read()}
        print("  ", hex_path, len(fw[key]["hex"]), "chars")

    with open(APP, encoding="utf-8") as f:
        html = f.read()
    blob = json.dumps(fw, separators=(",", ":"))
    html, n = re.subn(r"/\*FW-BEGIN\*/.*?/\*FW-END\*/",
                      lambda m: "/*FW-BEGIN*/" + blob + "/*FW-END*/", html, count=1, flags=re.S)
    if n != 1:
        sys.exit("FW-BEGIN / FW-END markers not found in " + APP)
    with open(APP, "w", encoding="utf-8", newline="") as f:
        f.write(html)
    print("embedded firmware", ver, "in", APP)


if __name__ == "__main__":
    main()
