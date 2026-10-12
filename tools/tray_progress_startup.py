"""Makes tools/tray_progress.ps1 (the port's progress next to the taskbar clock) start with
Windows: a small .vbs in the user's Startup folder that runs it without a console window.
Also starts it now. `--remove` takes it out of Startup.

    python tools/tray_progress_startup.py [--remove]
"""
import os
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / 'tray_progress.ps1'
STARTUP = Path(os.environ['APPDATA']) / 'Microsoft' / 'Windows' / 'Start Menu' / 'Programs' / 'Startup'
LAUNCHER = STARTUP / 'Moonlighter port progress.vbs'
COMMAND = f'powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "{SCRIPT}"'


def main():
    if '--remove' in sys.argv:
        LAUNCHER.unlink(missing_ok=True)
        print(f'removed {LAUNCHER}')
        return
    LAUNCHER.write_text(f'CreateObject("WScript.Shell").Run "{COMMAND.replace(chr(34), chr(34) * 2)}", 0, False\n', encoding='utf-8')
    print(f'installed {LAUNCHER}')
    subprocess.Popen(['wscript', str(LAUNCHER)])
    print('started')


if __name__ == '__main__':
    main()
