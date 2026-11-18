"""Shared metre-space terrain and route definitions for Blender and Unreal authoring."""
import math

CENTERS = [(-340, -160, 70, 34), (-140, 0, 26, 48), (170, 100, 42, 57)]
PATH = [(-340, -160), (-275, -128), (-235, -58), (-140, 0), (-60, -35), (30, -8), (105, 65), (170, 100)]


def smooth(a, b, x):
    t = max(0, min(1, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def noise(x, y):
    ix, iy = math.floor(x), math.floor(y)
    tx, ty = smooth(0, 1, x - ix), smooth(0, 1, y - iy)
    def corner(a, b):
        value = ((a * 374761393 + b * 668265263 + 1030) & 0xffffffff)
        value = ((value ^ (value >> 13)) * 1274126177) & 0xffffffff
        return ((value ^ (value >> 16)) & 0xffff) / 32767.5 - 1
    a = corner(ix, iy) * (1 - tx) + corner(ix + 1, iy) * tx
    b = corner(ix, iy + 1) * (1 - tx) + corner(ix + 1, iy + 1) * tx
    return a * (1 - ty) + b * ty


def height(x, y):
    z = 28 + 12 * math.sin(x * .014 + .5) * math.cos(y * .011) + 4 * math.sin(x * .041 + y * .028)
    edge = max(abs(x) / 500, abs(y) / 500)
    z += smooth(.48, 1.05, edge) * (100 + 75 * (.5 + .5 * math.sin(x * .022 - y * .019)))
    z += smooth(.48, .88, edge) * (17 * noise(x * .031, y * .031) + 7 * noise(x * .073, y * .073) + 2.5 * noise(x * .14, y * .14))
    for cx, cy, flat, radius in CENTERS:
        weight = 1 - smooth(radius, radius + 42, math.hypot(x - cx, y - cy))
        z = z * (1 - weight) + flat * weight
    return z


def surface_height(x, y):
    """Match the actual 3.125m triangulated terrain, including its face diagonal."""
    cell = 125 / 40
    ax, ay = math.floor(x / cell) * cell, math.floor(y / cell) * cell
    u, v = (x - ax) / cell, (y - ay) / cell
    a, b = height(ax, ay), height(ax + cell, ay)
    c, d = height(ax, ay + cell), height(ax + cell, ay + cell)
    return a + (b - a) * u + (d - b) * v if u >= v else a + (d - c) * u + (c - a) * v


def road_weight(x, y):
    distance = 1e9
    for a, b in zip(PATH, PATH[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        t = max(0, min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy)))
        distance = min(distance, math.hypot(x - a[0] - dx * t, y - a[1] - dy * t))
    weight = 1 - smooth(2.2, 5, distance)
    for cx, cy, _, radius in CENTERS:
        weight = max(weight, (1 - smooth(radius * .5, radius * .85, math.hypot(x - cx, y - cy))) * .9)
    return weight
