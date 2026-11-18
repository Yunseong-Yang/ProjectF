"""Derive a topographic map from the same local height function as the terrain."""
from pathlib import Path
import math
import numpy as np
from PIL import Image
from frontier_layout import height, road_weight

root = Path(__file__).resolve().parents[1]
size = 768
heights = np.empty((size, size), dtype=np.float32)
roads = np.empty_like(heights)
for row in range(size):
    for col in range(size):
        x, y = (0.5 - row / (size - 1)) * 1000, (col / (size - 1) - .5) * 1000
        heights[row, col] = height(x, y); roads[row, col] = road_weight(x, y)
dx, dy = np.gradient(heights)
shade = np.clip(.7 + .22 * (dx - dy), .25, 1)
rock = np.clip((np.hypot(dx, dy) - .3) / 1.4, 0, 1)
color = np.array([.17, .23, .18])[None, None, :] * (1 - rock[:, :, None]) + np.array([.3, .31, .28])[None, None, :] * rock[:, :, None]
color = color * (1 - roads[:, :, None] * .7) + np.array([.48, .39, .24])[None, None, :] * roads[:, :, None] * .7
contour = np.abs(np.mod(heights, 10) - 5) < .25
color *= (shade * np.where(contour, .68, 1))[:, :, None]
output = root / "ArtSource/Environment/T_FrontierMap.png"
Image.fromarray(np.uint8(np.clip(color * 255, 0, 255))).save(output)
print(output)
