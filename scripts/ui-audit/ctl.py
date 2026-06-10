#!/usr/bin/env python3
"""Harnais d'audit UI pleNx (desktop macOS).

Pilote l'application au clavier (osascript / System Events) et capture la
fenêtre (screencapture). Le terminal qui l'exécute doit avoir les permissions
macOS « Accessibilité » et « Enregistrement d'écran ».

Mapping clavier borealis desktop (library/borealis/.../glfw_input.cpp:266) :
flèches = D-pad, Entrée = A (valider), Échap = B (retour).

Usage :
  ctl.py launch                # démarre l'app si besoin et la met au premier plan
  ctl.py key down down a       # envoie des touches : up/down/left/right/a/b
  ctl.py shot home             # capture la fenêtre -> SHOTS_DIR/home.png
  ctl.py click 240 320         # clic souris (coordonnées canvas, repère des shots)
  ctl.py longclick 240 320 800 # clic maintenu N ms (défaut 700) sans bouger
  ctl.py run scenarios/x.txt   # rejoue un scénario (launch/key/shot/sleep/quit)
  ctl.py quit                  # ferme l'app

Les clics passent par cliclick (brew install cliclick) : coordonnées écran
absolues = position fenêtre + TITLEBAR + offset canvas. Les coordonnées
attendues sont en points du canvas (fenêtre 1280x720) ; les captures `shot`
étant Retina 2x (2560x1440), diviser par 2 les pixels lus sur un shot.

Scénario : un fichier texte, une commande par ligne (# = commentaire) :
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
TITLEBAR = 32  # points ; fenêtre 1280x752 pour un canvas 1280x720
KEY_DELAY = float(os.environ.get("KEY_DELAY", "0.4"))

KEYS = {"up": 126, "down": 125, "left": 123, "right": 124, "a": 36, "b": 53,
        # raccourcis applicatifs (utils/keybind.cpp, défauts de config.cpp:333+)
        "f4": 118, "f5": 96}  # f4 = menu contextuel (X), f5 = rafraîchir


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
    for _ in range(30):  # fenêtre + premier chargement réseau
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
    """Appui bref : le patch « sticky keys » de borealis (glfw_input.cpp)
    garantit qu'une frappe plus courte qu'une frame produit exactement UNE
    navigation ; un hold sous ~16 ms évite toute répétition."""
    try:
        import Quartz
        for pressed in (True, False):
            ev = Quartz.CGEventCreateKeyboardEvent(None, code, pressed)
            Quartz.CGEventPost(Quartz.kCGHIDEventTap, ev)
            if pressed:
                time.sleep(hold)
    except ImportError:
        # repli : frappe atomique (peut être ratée par le polling)
        osa(f'tell application "System Events" to key code {code}')


def shot(name):
    activate()
    x, y, w, h = bounds()
    os.makedirs(SHOTS_DIR, exist_ok=True)
    path = os.path.join(SHOTS_DIR, f"{name}.png")
    subprocess.run(["screencapture", "-x", "-R", f"{x},{y + TITLEBAR},{w},{h - TITLEBAR}", path])
    print(path)


def to_screen(cx, cy):
    """Coordonnées canvas (repère des shots) -> coordonnées écran absolues."""
    x, y, _w, _h = bounds()
    return x + int(cx), y + TITLEBAR + int(cy)


def click(cx, cy):
    activate()
    sx, sy = to_screen(cx, cy)
    subprocess.run(["cliclick", f"c:{sx},{sy}"])
    time.sleep(KEY_DELAY)


def longclick(cx, cy, ms=700):
    """Press maintenu immobile (drag-down, attente, drag-up au même point)."""
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
