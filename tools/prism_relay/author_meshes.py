#!/usr/bin/env python3
"""Author original Prism Relay hard-surface meshes; no runtime dependency.
生成原创 Prism Relay 硬表面网格；游戏运行不依赖此工具。
"""

import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2] / 'examples/PrismRelay/assets/meshes'


def normalized(value):
    length = math.sqrt(sum(v * v for v in value))
    return [v / length for v in value]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]]


class Mesh:
    """Separate corners retain face UV seams and smooth chamfer normals.
    独立面角保留 UV 接缝及倒角的平滑法线。
    """

    def __init__(self):
        self.vertices = []
        self.indices = []

    def vertex(self, point, normal, uv, color=(1, 1, 1, 1)):
        index = len(self.vertices)
        self.vertices.append(dict(position=point, color=color, normal=normal,
                                  texCoord=uv, tangent=[0, 0, 0, 0]))
        return index

    def triangle(self, a, b, c):
        points = [self.vertices[i]['position'] for i in (a, b, c)]
        ab = [points[1][i] - points[0][i] for i in range(3)]
        ac = [points[2][i] - points[0][i] for i in range(3)]
        face = cross(ab, ac)
        outward = self.vertices[a]['normal']

        if sum(face[i] * outward[i] for i in range(3)) < 0:
            b, c = c, b

        self.indices.extend((a, b, c))

    def box(self, center, size, bevel=.02, color=(1, 1, 1, 1), tile=1):
        half = [s / 2 for s in size]
        bevel = min(bevel, min(half) * .45)
        core = [s - bevel for s in half]

        for axis in range(3):
            u, v = (axis + 1) % 3, (axis + 2) % 3
            us = [-half[u], -core[u], core[u], half[u]]
            vs = [-half[v], -core[v], core[v], half[v]]

            for sign in (-1, 1):
                grid = []

                for y in vs:
                    row = []

                    for x in us:
                        point = [0, 0, 0]
                        point[axis], point[u], point[v] = sign * half[axis], x, y
                        inner = [max(-core[i], min(core[i], point[i])) for i in range(3)]
                        normal = normalized([point[i] - inner[i] for i in range(3)])
                        rounded = [center[i] + inner[i] + normal[i] * bevel for i in range(3)]
                        row.append(self.vertex(rounded, normal,
                                               [(x + half[u]) * (tile[u] if isinstance(tile, tuple) else tile),
                                                (y + half[v]) * (tile[v] if isinstance(tile, tuple) else tile)], color))

                    grid.append(row)

                for j in range(3):
                    for i in range(3):
                        self.triangle(grid[j][i], grid[j][i + 1], grid[j + 1][i + 1])
                        self.triangle(grid[j][i], grid[j + 1][i + 1], grid[j + 1][i])

    def cylinder(self, center, radius, height, color=(1, 1, 1, 1), segments=40):
        for segment in range(segments):
            angles = [2 * math.pi * segment / segments, 2 * math.pi * (segment + 1) / segments]
            side = []

            for y in (-height / 2, height / 2):
                for angle in angles:
                    normal = [math.cos(angle), 0, math.sin(angle)]
                    point = [center[0] + radius * normal[0], center[1] + y,
                             center[2] + radius * normal[2]]
                    side.append(self.vertex(point, normal, [angle / (2 * math.pi), y / height + .5], color))

            self.triangle(side[0], side[1], side[3])
            self.triangle(side[0], side[3], side[2])

            for sign in (-1, 1):
                y = center[1] + sign * height / 2
                middle = self.vertex([center[0], y, center[2]], [0, sign, 0], [.5, .5], color)
                edge = [self.vertex([center[0] + radius * math.cos(a), y,
                                     center[2] + radius * math.sin(a)], [0, sign, 0],
                                    [.5 + .5 * math.cos(a), .5 + .5 * math.sin(a)], color)
                        for a in angles]
                self.triangle(middle, *edge)

    def ring(self, center, radius, tube, color=(1, 1, 1, 1)):
        grid = []

        for i in range(49):
            theta = 2 * math.pi * i / 48
            row = []

            for j in range(9):
                phi = 2 * math.pi * j / 8
                normal = [math.cos(theta) * math.cos(phi), math.sin(phi),
                          math.sin(theta) * math.cos(phi)]
                point = [center[0] + radius * math.cos(theta) + tube * normal[0],
                         center[1] + tube * normal[1],
                         center[2] + radius * math.sin(theta) + tube * normal[2]]
                row.append(self.vertex(point, normal, [i / 48, j / 8], color))

            grid.append(row)

        for i in range(48):
            for j in range(8):
                self.triangle(grid[i][j], grid[i + 1][j], grid[i + 1][j + 1])
                self.triangle(grid[i][j], grid[i + 1][j + 1], grid[i][j + 1])

    def save(self, name):
        def rounded(value):
            if isinstance(value, float):
                return round(value, 7)
            if isinstance(value, (list, tuple)):
                return [rounded(v) for v in value]
            if isinstance(value, dict):
                return {k: rounded(v) for k, v in value.items()}
            return value

        # Keep one vertex per line: authored values remain inspectable without huge arrays.
        # 每行一个顶点，便于审查数值而不展开成海量数组行。
        vertices = ',\n'.join('    ' + json.dumps(rounded(v), separators=(',', ':')) for v in self.vertices)
        indices = ', '.join(str(i) for i in self.indices)
        (ROOT / f'{name}.dotmesh').write_text(
            '{\n  "vertices": [\n' + vertices + '\n  ],\n  "indices": [' + indices + ']\n}\n')


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    # Faceted quartz owns explicit flat normals; PBR never substitutes missing normals.
    # 棱面晶体带显式平面法线；PBR 不替代缺失法线。
    crystal = Mesh()
    points = [[0, 1, 0], [0, -1, 0]]

    for y in (-.5, .5):
        points.extend([[.65 * math.cos(i * math.pi / 3), y,
                        .65 * math.sin(i * math.pi / 3)] for i in range(6)])

    for i in range(6):
        j = (i + 1) % 6
        faces = [(0, 8 + i, 8 + j), (1, 2 + j, 2 + i),
                 (2 + i, 2 + j, 8 + j), (2 + i, 8 + j, 8 + i)]

        for face in faces:
            a, b, c = [points[k] for k in face]
            normal = normalized(cross([b[k] - a[k] for k in range(3)],
                                      [c[k] - a[k] for k in range(3)]))
            middle = [sum(points[k][axis] for k in face) / 3 for axis in range(3)]

            if sum(middle[k] * normal[k] for k in range(3)) < 0:
                normal = [-v for v in normal]

            ids = [crystal.vertex(points[k], normal, uv)
                   for k, uv in zip(face, [(0, 0), (1, 0), (1, 1)])]
            crystal.triangle(*ids)

    lines = []

    for vertex in crystal.vertices:
        lines.append('v ' + ' '.join(f'{v:.7f}' for v in vertex['position']))

    for vertex in crystal.vertices:
        lines.append('vn ' + ' '.join(f'{v:.7f}' for v in vertex['normal']))

    for i in range(0, len(crystal.indices), 3):
        lines.append('f ' + ' '.join(f'{k + 1}//{k + 1}' for k in crystal.indices[i:i + 3]))

    (ROOT.parent / 'crystal.obj').write_text('\n'.join(lines) + '\n')
    cube = Mesh()
    cube.box([0, 0, 0], [1, 1, 1], .045)
    cube.save('chamfered-block')
    floor = Mesh()
    floor.box([0, 0, 0], [1, 1, 1], .004, tile=5)
    floor.save('concrete-slab')
    apron = Mesh()
    apron.box([0, 0, 0], [60, .6, 60], .08, tile=1 / 3)
    apron.save('service-apron')
    wall = Mesh()
    wall.box([0, 0, 0], [1, 1, 1], .015, tile=(5, 3.2 / 3, .1))
    wall.save('concrete-wall')
    louvre = Mesh()

    for y in (-.22, -.11, 0, .11, .22):
        louvre.box([0, y, 0], [1.7, .045, .06], .009)

    louvre.save('intake-louvre')
    cooling = Mesh()

    for z in (-.55, 0, .55):
        cooling.box([0, 0, z], [.08, 1.9, .18], .02)

    cooling.save('cabinet-cooling')
    panel = Mesh()
    panel.box([0, 0, 0], [2.84, .045, 2.84], .012, tile=2)

    for x in (-1.3, 1.3):
        for z in (-1.3, 1.3):
            panel.cylinder([x, .026, z], .025, .01, [.35, .35, .35, 1], 12)

    panel.save('deck-panel')
    drone = Mesh()
    drone.box([0, -.06, 0], [.56, .35, .7], .08)
    drone.box([0, -.14, -.35], [.4, .13, .12], .025)
    drone.save('drone-shell')
    hardware = Mesh()

    for x in (-.32, .32):
        for z in (-.28, .28):
            hardware.cylinder([x, -.06, z], .15, .10)
            hardware.ring([x, .01, z], .115, .024)
            hardware.box([x, .005, z], [.2, .014, .035], .005)

    hardware.box([0, .11, .04], [.28, .045, .37], .01)
    hardware.save('drone-hardware')
    eye = Mesh()
    eye.box([0, -.07, -.411], [.22, .075, .018], .008)
    eye.save('drone-optics')
    relay = Mesh()
    relay.cylinder([0, -.46, 0], .48, .08)
    relay.cylinder([0, .46, 0], .48, .08)
    relay.box([0, 0, 0], [.67, .86, .67], .05)

    for y in (-.25, 0, .25):
        relay.cylinder([0, y, 0], .46, .026)

    relay.save('relay-column')
    rails = Mesh()

    for x in (-.28, .28):
        for z in (-.35, .35):
            rails.box([x, 0, z], [.055, .76, .025], .006)

    rails.save('relay-indicators')
    hazard = Mesh()
    hazard.box([0, -.46, 0], [.99, .08, .99], .025, [.12, .14, .16, 1])

    for x in (-.38, -.19, 0, .19, .38):
        hazard.box([x, -.36, 0], [.065, .1, .8], .015, [.65, .12, .025, 1])

    hazard.save('hazard-grille')
    plinth = Mesh()
    plinth.cylinder([0, .04, 0], .4, .08)
    plinth.ring([0, .09, 0], .32, .025)
    plinth.save('cell-dock')


if __name__ == '__main__':
    main()
