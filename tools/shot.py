"""One on-demand full-resolution screenshot of the game (banner material) - no background recording (the owner,
2026-09-29). Usage: python tools/shot.py <name>  ->  screenshots/<name>.png. Refuses unless the game is in front."""
import os
import subprocess
import sys

from PIL import ImageGrab

name = sys.argv[1]
front = subprocess.run([sys.executable, r"D:\Claude output\.MD\scripts\game_foreground.py"], capture_output=True, text=True)
if front.returncode != 0:
    print("refused: the game is not the window in front -", front.stdout.strip())
    sys.exit(1)
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "screenshots")
os.makedirs(out, exist_ok=True)
path = os.path.join(out, name + ".png")
img = ImageGrab.grab()
img.save(path)
print("saved", path, img.size)
