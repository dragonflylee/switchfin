#!/usr/bin/env python3
"""pleNx UI audit harness (desktop macOS).

Drives the application with the keyboard (osascript / System Events) and
captures the window (screencapture). The terminal running it must have the
macOS "Accessibility" and "Screen Recording" permissions.

Borealis desktop keyboard mapping (library/borealis/.../glfw_input.cpp:266):
arrows = D-pad, Enter = A (confirm), Escape = B (back).

Usage:
  ctl.py launch                # starts the app if needed and brings it to front
  ctl.py key down down a       # sends keys: up/down/left/right/a/b
  ctl.py shot home             # captures the window -> SHOTS_DIR/home.png
  ctl.py click 240 320         # mouse click (canvas coordinates, shot frame)
  ctl.py longclick 240 320 800 # held click for N ms (default 700) without moving
  ctl.py run scenarios/x.txt   # replays a scenario (launch/key/shot/sleep/quit)
  ctl.py quit                  # closes the app

Clicks go through cliclick (brew install cliclick): absolute screen
coordinates = window position + TITLEBAR + canvas offset. Expected
coordinates are in canvas points (1280x720 window); `shot` captures being
Retina 2x (2560x1440), divide pixels read on a shot by 2.

Scenario: a text file, one command per line (# = comment):
  launch
  key down down right
  sleep 1.5
  shot films-grille
  click 240 320
  longclick 240 320 800
"""

import os
import subprocess
import sys
import time

APP = "pleNx"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(ROOT, "build_desktop", "pleNx.app", "Contents", "MacOS", "pleNx")
SHOTS_DIR = os.environ.get("SHOTS_DIR", "/tmp/sx-shots")
TITLEBAR = 32  # points; 1280x752 window for a 1280x720 canvas
KEY_DELAY = float(os.environ.get("KEY_DELAY", "0.4"))

KEYS = {"up": 126, "down": 125, "left": 123, "right": 124, "a": 36, "b": 53,
        # application shortcuts (utils/keybind.cpp, defaults in config.cpp:333+)
        "f4": 118, "f5": 96}  # f4 = context menu (X), f5 = refresh


def osa(*lines):
    args = ["osascript"]
    for line in lines:
        args += ["-e", line]
    return subprocess.run(args, capture_output=True, text=True)


def running():
    return subprocess.run(["pgrep", "-x", APP], capture_output=True).returncode == 0


def activate():
    osa(f'tell application "System Events" to set frontmost of (first process whose name is "{APP}") to true')
    time.sleep(0.5)


def window_ready():
    r = osa(
        f'tell application "System Events" to tell (first process whose name is "{APP}")'
        " to get position of window 1"
    )
    return r.returncode == 0


def launch():
    if not running():
        subprocess.Popen([BINARY], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(30):  # window + first network load
        if window_ready():
            break
        time.sleep(1)
    else:
        sys.exit(f"l'app {APP} n'a pas ouvert de fenêtre après 30 s")
    time.sleep(2)
    activate()


def quit_app():
    if running():
        subprocess.run(["pkill", "-x", APP])
        time.sleep(1)


def bounds():
    r = osa(
        f'tell application "System Events" to tell (first process whose name is "{APP}")'
        " to get {position, size} of window 1"
    )
    if r.returncode != 0:
        sys.exit(f"fenêtre {APP} introuvable : {r.stderr.strip()}")
    return [int(v.strip()) for v in r.stdout.strip().split(",")]


def key(names):
    activate()
    for name in names:
        code = KEYS[name.lower()]
        _press(code)
        time.sleep(KEY_DELAY)


def _press(code, hold=0.012):
    """Short press: the borealis "sticky keys" patch (glfw_input.cpp)
    guarantees that a keystroke shorter than a frame produces exactly ONE
    navigation; a hold under ~16 ms avoids any repeat."""
    try:
        import Quartz
        for pressed in (True, False):
            ev = Quartz.CGEventCreateKeyboardEvent(None, code, pressed)
            Quartz.CGEventPost(Quartz.kCGHIDEventTap, ev)
            if pressed:
                time.sleep(hold)
    except ImportError:
        # fallback: atomic keystroke (may be missed by the polling)
        osa(f'tell application "System Events" to key code {code}')


def shot(name):
    activate()
    x, y, w, h = bounds()
    os.makedirs(SHOTS_DIR, exist_ok=True)
    path = os.path.join(SHOTS_DIR, f"{name}.png")
    subprocess.run(["screencapture", "-x", "-R", f"{x},{y + TITLEBAR},{w},{h - TITLEBAR}", path])
    print(path)


def to_screen(cx, cy):
    """Canvas coordinates (shot frame) -> absolute screen coordinates."""
    x, y, _w, _h = bounds()
    return x + int(cx), y + TITLEBAR + int(cy)


def click(cx, cy):
    activate()
    sx, sy = to_screen(cx, cy)
    subprocess.run(["cliclick", f"c:{sx},{sy}"])
    time.sleep(KEY_DELAY)


def longclick(cx, cy, ms=700):
    """Held press without moving (drag-down, wait, drag-up at the same point)."""
    activate()
    sx, sy = to_screen(cx, cy)
    subprocess.run(["cliclick", f"dd:{sx},{sy}", f"w:{int(ms)}", f"du:{sx},{sy}"])
    time.sleep(KEY_DELAY)


def run_scenario(path):
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            cmd, *args = line.split()
            if cmd == "launch":
                launch()
            elif cmd == "quit":
                quit_app()
            elif cmd == "key":
                key(args)
            elif cmd == "shot":
                shot(args[0])
            elif cmd == "click":
                click(*args[:2])
            elif cmd == "longclick":
                longclick(*args[:3])
            elif cmd == "sleep":
                time.sleep(float(args[0]))
            else:
                sys.exit(f"commande inconnue : {line}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, *args = sys.argv[1:]
    if cmd == "launch":
        launch()
    elif cmd == "quit":
        quit_app()
    elif cmd == "key":
        key(args)
    elif cmd == "shot":
        shot(args[0])
    elif cmd == "click":
        click(*args[:2])
    elif cmd == "longclick":
        longclick(*args[:3])
    elif cmd == "run":
        run_scenario(args[0])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
