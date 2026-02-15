import atexit
import os
import subprocess
import sys
import time
from pathlib import Path

REAPER_PATH = r"C:\Program Files\REAPER (x64)\reaper.exe"
REASCRIPT_PATH = os.path.join(
    os.path.dirname(os.path.realpath(__file__)), "rea_midi_export.lua"
)

subprocs = []
atexit.register(lambda: [p.kill() for p in subprocs])


def run_export(rpp_path: Path):
    midi_path = rpp_path.with_suffix(".mid")
    state = midi_path.stat().st_mtime if midi_path.exists() else None

    os.environ["REAPER_EXPORT_MIDI_PATH"] = str(midi_path)
    proc = subprocess.Popen(
        [
            REAPER_PATH,
            "-newinst",
            "-nosplash",
            str(rpp_path),
            str(REASCRIPT_PATH),
        ],
    )
    subprocs.append(proc)

    while proc.poll() is None:
        new_state = midi_path.stat().st_mtime if midi_path.exists() else None
        if new_state != state:
            while new_state != state:
                time.sleep(0.1)  # wait for write to complete
                state = new_state
                new_state = midi_path.stat().st_mtime if midi_path.exists() else None

            print(f"Exported MIDI: {midi_path}")
            proc.kill()
            return True
        time.sleep(0.5)

    print("Error: REAPER process exited without exporting MIDI.")
    proc.kill()
    return False


def main(rpp_path):
    success = run_export(rpp_path)
    if not success:
        sys.exit(1)


if __name__ == "__main__":
    rpp_path = Path(sys.argv[1]).resolve()
    if not rpp_path.is_file():
        print(f"Error: {rpp_path} is not a valid file.")
        sys.exit(1)
    main(rpp_path)
