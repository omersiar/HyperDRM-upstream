"""What can sixteen overlapping rectangle averages tell us about a colour grid?

LG's histogram service returns one 256-bin histogram per region, and each region
is a rectangle of the video plane, so each region colour is an *area mean*. That
makes the sixteen measurements `m` and a grid of per-cell colours `x` related by
a fixed matrix `m = A x`, with `A[r][c]` the fraction of region r inside cell c.

This tool asks how much of `x` that system can recover, and at what cost. It is
offline: no TV access, no video frames, no DRM handling. Rectangles are the
measured profile in profiles/65QNED87A6B.BEUFLJP.json (the block scan on the G4
agreed with it within 1-9 %).

Reconstructions, each producing `cells = M @ m` or an equivalent field:

  select    smallest window containing the cell -- what the service does today
  voronoi   nearest region centre
  min-norm  least squares with no prior (the lambda -> 0 limit)
  basis     bilinear tensor basis of bx*by control points
  reg       (A^T A + lam L^T L) x = A^T m with mu = 0, solved by conjugate
            gradients -- the handoff's method without the self-fulfilling prior
  ...cl     the same field clamped to the per-channel range of the cells' own
            windows

Scores, all in sRGB codes:

  LED    worst-channel error at the rim positions, where the LEDs are
  step%  share of neighbouring *cells* whose step differs from the truth by
         more than 8 codes: a mosaic invents steps, a blur erases them
  rim%   the same, between neighbouring LEDs
  cell   worst-channel error over every cell

Run: python3 tools/histogram_reconstruct.py
"""

import math
import random

GX, GY = 32, 18

# region: x0, y0, x1, y1 (normalised, measured)
RECTS = [
    (0.24, 0.00, 0.75, 0.98), (0.19, 0.79, 0.80, 0.99), (0.29, 0.29, 0.70, 0.70),
    (0.09, 0.01, 0.40, 0.18), (0.00, 0.00, 0.50, 0.98), (0.00, 0.65, 0.28, 0.99),
    (0.00, 0.22, 0.19, 0.75), (0.00, 0.00, 0.29, 0.36), (0.49, 0.01, 0.99, 0.98),
    (0.69, 0.65, 0.99, 0.99), (0.79, 0.22, 0.99, 0.75), (0.34, 0.01, 0.65, 0.18),
    (0.59, 0.00, 0.89, 0.19), (0.69, 0.01, 0.98, 0.36), (0.29, 0.00, 0.70, 0.12),
    (0.01, 0.29, 0.12, 0.70),
]

# Regions the shipped operator does not use. Both are panel-anchored probes the
# firmware does not scale with the picture: r14 is the top bar probe and r15 a
# left strip, each barely a tenth of the panel tall or wide. Their means differ
# from the areas around them, and because the fit is global, each of them pulled
# its own neighbourhood towards itself on the wall. Dropping the two rows costs
# two of sixteen constraints and removes that pull.
EXCLUDED_REGIONS = (14, 15)


# --- dense linear algebra (stdlib only) -----------------------------------
def matmul(a, b):
    return [[sum(row[k] * b[k][j] for k in range(len(b))) for j in range(len(b[0]))]
            for row in a]


def transpose(a):
    return [list(col) for col in zip(*a)]


def identity(n):
    return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]


def inverse(a):
    """Gauss-Jordan with partial pivoting; None when singular."""
    n = len(a)
    aug = [row[:] + identity(n)[i] for i, row in enumerate(a)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(aug[r][col]))
        if abs(aug[pivot][col]) < 1e-12:
            return None
        aug[col], aug[pivot] = aug[pivot], aug[col]
        scale = aug[col][col]
        aug[col] = [v / scale for v in aug[col]]
        for r in range(n):
            if r != col and aug[r][col]:
                factor = aug[r][col]
                aug[r] = [v - factor * w for v, w in zip(aug[r], aug[col])]
    return [row[n:] for row in aug]


def eigenvalues(a, sweeps=60):
    """Jacobi rotations for a symmetric matrix, descending."""
    n = len(a)
    m = [row[:] for row in a]
    for _ in range(sweeps):
        off = math.sqrt(sum(m[i][j] ** 2 for i in range(n) for j in range(n) if i != j))
        if off < 1e-12:
            break
        for p in range(n - 1):
            for q in range(p + 1, n):
                if abs(m[p][q]) < 1e-15:
                    continue
                theta = (m[q][q] - m[p][p]) / (2 * m[p][q])
                t = math.copysign(1.0, theta) / (abs(theta) + math.sqrt(theta * theta + 1))
                c = 1 / math.sqrt(t * t + 1)
                s = t * c
                for k in range(n):
                    mkp, mkq = m[k][p], m[k][q]
                    m[k][p], m[k][q] = c * mkp - s * mkq, s * mkp + c * mkq
                for k in range(n):
                    mpk, mqk = m[p][k], m[q][k]
                    m[p][k], m[q][k] = c * mpk - s * mqk, s * mpk + c * mqk
    return sorted((m[i][i] for i in range(n)), reverse=True)


# --- geometry -------------------------------------------------------------
def coverage(gx, gy, rects=None):
    """A[r][c] = fraction of region r's area inside cell c."""
    rects = RECTS if rects is None else rects
    xs, ys = [i / gx for i in range(gx + 1)], [j / gy for j in range(gy + 1)]
    a = []
    for x0, y0, x1, y1 in rects:
        area = (x1 - x0) * (y1 - y0)
        row = []
        for j in range(gy):
            oy = max(0.0, min(y1, ys[j + 1]) - max(y0, ys[j]))
            for i in range(gx):
                ox = max(0.0, min(x1, xs[i + 1]) - max(x0, xs[i]))
                row.append(ox * oy / area)
        a.append(row)
    return a


def perturb(rects, rng, sigma):
    """Edge noise comparable to the block scan's own fit error."""
    out = []
    for x0, y0, x1, y1 in rects:
        d = [rng.gauss(0.0, sigma) for _ in range(4)]
        vals = tuple(min(1.0, max(0.0, v)) for v in (x0 + d[0], y0 + d[1], x1 + d[2], y1 + d[3]))
        out.append(vals if vals[2] - vals[0] > 0.02 and vals[3] - vals[1] > 0.02 else (x0, y0, x1, y1))
    return out


# --- reconstructions ------------------------------------------------------
def select_matrix(gx, gy):
    """Smallest window containing the cell centre, else the nearest centre."""
    centres = [((x0 + x1) / 2, (y0 + y1) / 2) for x0, y0, x1, y1 in RECTS]
    areas = [(x1 - x0) * (y1 - y0) for x0, y0, x1, y1 in RECTS]
    m = []
    for j in range(gy):
        for i in range(gx):
            px, py = (i + 0.5) / gx, (j + 0.5) / gy
            best = -1
            for r, (x0, y0, x1, y1) in enumerate(RECTS):
                if x0 <= px <= x1 and y0 <= py <= y1 and (best < 0 or areas[r] < areas[best]):
                    best = r
            if best < 0:
                best = min(range(16), key=lambda r: (px - centres[r][0]) ** 2 + (py - centres[r][1]) ** 2)
            row = [0.0] * 16
            row[best] = 1.0
            m.append(row)
    return m


def voronoi_matrix(gx, gy):
    centres = [((x0 + x1) / 2, (y0 + y1) / 2) for x0, y0, x1, y1 in RECTS]
    m = []
    for j in range(gy):
        for i in range(gx):
            px, py = (i + 0.5) / gx, (j + 0.5) / gy
            row = [0.0] * 16
            row[min(range(16), key=lambda r: (px - centres[r][0]) ** 2 + (py - centres[r][1]) ** 2)] = 1.0
            m.append(row)
    return m


def min_norm_matrix(a):
    """A^T (A A^T)^-1: least squares with no prior at all."""
    inv = inverse(matmul(a, transpose(a)))
    if inv is None:
        raise SystemExit("A A^T is singular")
    return matmul(transpose(a), inv)


def basis_matrix(a, bx, by, gx=GX, gy=GY):
    """Field = bilinear tensor basis of bx*by control points."""
    def hat(u, n):
        s = u * (n - 1)
        i = min(n - 2, max(0, int(math.floor(s))))
        return i, s - i

    b = []
    for j in range(gy):
        iy, fy = hat((j + 0.5) / gy, by)
        for i in range(gx):
            ix, fx = hat((i + 0.5) / gx, bx)
            row = [0.0] * (bx * by)
            for wy, ny in ((1 - fy, iy), (fy, iy + 1)):
                for wx, nx in ((1 - fx, ix), (fx, ix + 1)):
                    row[ny * bx + nx] += wy * wx
            b.append(row)
    ab = matmul(a, b)
    inv = inverse(matmul(transpose(ab), ab))
    if inv is None:
        raise SystemExit("A B is singular")
    return matmul(b, matmul(inv, transpose(ab)))


def laplacian_apply(v, gx=GX, gy=GY):
    out = [0.0] * (gx * gy)
    for j in range(gy):
        for i in range(gx):
            k = j * gx + i
            acc, degree = 0.0, 0
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ii, jj = i + di, j + dj
                if 0 <= ii < gx and 0 <= jj < gy:
                    acc += v[jj * gx + ii]
                    degree += 1
            out[k] = degree * v[k] - acc
    return out


def regularised_field(a, means, lam, iterations=400, clamp=True):
    """(A^T A + lam L^T L) x = A^T m, mu = 0, by conjugate gradients.

    No prior: nothing pulls the answer towards the naive average. The operator is
    applied to a vector and never inverted, which is also how it would run on the
    TV, where a 576x576 inverse is out of the question.
    """
    n, nr, at = len(a[0]), len(a), transpose(a)

    def op(v):
        av = [sum(at[k][r] * v[k] for k in range(n)) for r in range(nr)]
        ata = [sum(at[k][r] * av[r] for r in range(nr)) for k in range(n)]
        lap = laplacian_apply(v)
        return [ata[k] + lam * lap[k] for k in range(n)]

    channels = []
    for c in range(3):
        b = [sum(at[k][r] * means[r][c] for r in range(nr)) for k in range(n)]
        x, r = [0.0] * n, b[:]
        p, rs = r[:], sum(v * v for v in r)
        for _ in range(iterations):
            kp = op(p)
            denom = sum(p[i] * kp[i] for i in range(n))
            if abs(denom) < 1e-18:
                break
            alpha = rs / denom
            x = [x[i] + alpha * p[i] for i in range(n)]
            r = [r[i] - alpha * kp[i] for i in range(n)]
            rs_new = sum(v * v for v in r)
            if rs_new < 1e-14:
                break
            beta = rs_new / rs
            p = [r[i] + beta * p[i] for i in range(n)]
            rs = rs_new
        channels.append(x)
    cells = [[channels[c][k] for c in range(3)] for k in range(n)]
    if not clamp:
        return cells
    return [[min(1.0, max(0.0, v)) for v in cell] for cell in cells]


def apply_matrix(m, means):
    return [[min(1.0, max(0.0, sum(row[r] * means[r][c] for r in range(len(row)))))
             for c in range(3)] for row in m]


def clamp_to_regions(field, means, a):
    """Keep every cell inside the per-channel range of its own windows."""
    out = []
    for k in range(len(field)):
        covering = [r for r in range(16) if a[r][k] > 0.0]
        if not covering:
            out.append(field[k])
            continue
        out.append([min(max(means[r][c] for r in covering),
                        max(min(means[r][c] for r in covering), field[k][c]))
                    for c in range(3)])
    return out


# --- scenes ---------------------------------------------------------------
def scenes():
    out = {}
    out["smooth gradient"] = [[0.05 + 0.55 * ((i + 0.5) / GX), 0.0,
                               0.60 - 0.55 * ((i + 0.5) / GX)] for j in range(GY) for i in range(GX)]
    hard = []
    for j in range(GY):
        for i in range(GX):
            if 2 <= j < 6 and 24 <= i < 30:
                hard.append([1.0, 1.0, 0.8])
            elif i < GX // 2:
                hard.append([0.6, 0.0, 0.0])
            else:
                hard.append([0.0, 0.0, 0.6])
    out["hard halves + patch"] = hard
    box = []
    for j in range(GY):
        for i in range(GX):
            box.append([0.0, 0.0, 0.0] if j < 3 or j >= GY - 3 else [0.5, 0.5, 0.5])
    out["letterbox"] = box
    room = []
    for j in range(GY):
        for i in range(GX):
            x, y = (i + 0.5) / GX, (j + 0.5) / GY
            c = [0.0, 0.0, 0.45 * (1 - y)]
            if 7 <= j < 12 and 12 <= i < 20:
                c = [0.55, 0.35, 0.25]
            if j < 6 and i < 8:
                c = [0.8, 0.8, 0.8]
            room.append(c)
    out["room + face + window"] = room
    return out


# --- metrics --------------------------------------------------------------
def to_srgb(v):
    v = min(1.0, max(0.0, v))
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


RIM = ([(x, 0.02) for x in [0.03 + 0.94 * t / 15 for t in range(16)]] +
       [(0.98, y) for y in [0.06 + 0.88 * t / 15 for t in range(16)]] +
       [(x, 0.98) for x in [0.97 - 0.94 * t / 15 for t in range(16)]] +
       [(0.02, y) for y in [0.94 - 0.88 * t / 15 for t in range(16)]])


def cell_of(point):
    return (min(GY - 1, int(point[1] * GY)) * GX + min(GX - 1, int(point[0] * GX)))


def led_error(field, truth):
    """Worst-channel error at the rim, where the LEDs are."""
    total = 0.0
    for point in RIM:
        k = cell_of(point)
        total += max(abs(to_srgb(field[k][c]) - to_srgb(truth[k][c])) for c in range(3))
    return 255 * total / (3 * len(RIM))


def step_error(field, truth, pairs, threshold=8.0):
    """Share of neighbouring pairs whose step differs from the truth's step.

    Counting raw jumps would punish a legitimate gradient. What matters is the
    difference from the true step: a mosaic invents steps, a blur erases them.
    """
    bad = 0
    for a, b in pairs:
        shown = 255 * max(abs(to_srgb(field[a][c]) - to_srgb(field[b][c])) for c in range(3))
        real = 255 * max(abs(to_srgb(truth[a][c]) - to_srgb(truth[b][c])) for c in range(3))
        bad += abs(shown - real) > threshold
    return 100.0 * bad / len(pairs)


def cell_steps():
    return ([(k, k + 1) for k in range(GX * GY - 1) if k % GX != GX - 1] +
            [(k, k + GX) for k in range(GX * GY - GX)])


def rim_steps():
    return [(cell_of(a), cell_of(b)) for a, b in zip(RIM, RIM[1:] + RIM[:1])]


def cell_error(field, truth):
    return 255 * sum(max(abs(to_srgb(field[k][c]) - to_srgb(truth[k][c])) for c in range(3))
                     for k in range(GX * GY)) / (3 * GX * GY)


# --- comparison -----------------------------------------------------------
def corrected_field(a, means, lam, iterations=400):
    """Regularised field plus the smallest correction that restores A x = m.

    The regulariser buys smoothness by giving up exact agreement with the
    measurements, which on a hard edge flattens a real boundary. The correction
    is the minimum-norm residual, so it adds back only the detail the fit was
    missing and leaves smooth content alone (its residual is already zero)."""
    field = regularised_field(a, means, lam, iterations, clamp=False)
    n = len(field)
    pinv = min_norm_matrix(a)
    residual = [[means[r][c] - sum(a[r][k] * field[k][c] for k in range(n)) for c in range(3)]
                for r in range(16)]
    return [[field[k][c] + sum(pinv[k][r] * residual[r][c] for r in range(16)) for c in range(3)]
            for k in range(n)]


def solver_set(a):
    precomputed = [("select", select_matrix(GX, GY)), ("voronoi", voronoi_matrix(GX, GY)),
                   ("min-norm", min_norm_matrix(a)), ("basis 2x2", basis_matrix(a, 2, 2)),
                   ("basis 3x3", basis_matrix(a, 3, 3))]
    fns = [(name, lambda m, am, M=M: apply_matrix(M, m)) for name, M in precomputed]
    for lam in (0.01, 0.05):
        fns.append((f"reg {lam}", lambda m, am, lam=lam: regularised_field(a, m, lam)))
        fns.append((f"reg {lam} cl",
                    lambda m, am, lam=lam: clamp_to_regions(regularised_field(a, m, lam), m, a)))
    fns.append(("reg .01 fix", lambda m, am: corrected_field(a, m, 0.01)))
    keep = [r for r in range(16) if r not in EXCLUDED_REGIONS]
    dropped = [[row[r] for r in keep] for row in build_operator(0.01, EXCLUDED_REGIONS, a)]
    fns.append(("reg -1415", lambda m, am: apply_matrix(dropped, [m[r] for r in keep])))
    fns.append(("min-norm cl",
                lambda m, am: clamp_to_regions(apply_matrix(min_norm_matrix(a), m), m, a)))
    return fns


def main():
    a = coverage(GX, GY)
    spectrum = eigenvalues(matmul(a, transpose(a)))
    sigma = [math.sqrt(max(v, 0.0)) for v in spectrum]
    print(f"A is {len(a)}x{len(a[0])}: singular values {sigma[0]:.3f} .. {sigma[-1]:.4f}, "
          f"condition {sigma[0] / max(sigma[-1], 1e-9):.1f}, "
          f"numerical rank {sum(1 for v in spectrum if v > spectrum[0] * 1e-9)}")
    print()

    rng = random.Random(1)
    variants = [("exact geometry", a),
                ("geometry +-0.02", coverage(GX, GY, perturb(RECTS, rng, 0.02))),
                ("geometry +-0.05", coverage(GX, GY, perturb(RECTS, rng, 0.05)))]
    solvers, steps_cell, steps_rim = solver_set(a), cell_steps(), rim_steps()
    for label, a_meas in variants:
        print(f"== {label}")
        print(f"{'solver':10} {'scene':20} {'LED':>6} {'step%':>6} {'rim%':>5} {'cell':>6}")
        for name, fn in solvers:
            for scene, truth in scenes().items():
                means = [[sum(a_meas[r][k] * truth[k][c] for k in range(GX * GY)) for c in range(3)]
                         for r in range(16)]
                field = fn(means, a_meas)
                print(f"{name:10} {scene:20} {led_error(field, truth):6.1f} "
                      f"{step_error(field, truth, steps_cell):6.1f} "
                      f"{step_error(field, truth, steps_rim):5.1f} "
                      f"{cell_error(field, truth):6.1f}")
        print()


# --- table emission -------------------------------------------------------
def build_operator(lam, excluded=(), a=None):
    """M[cell][region] for the regularised inverse, one unit vector per region.

    Excluded regions get a zero column: their measurements are dropped from the
    fit entirely rather than trusted with a small weight, which is the same
    thing the service would do by not passing them in."""
    a = coverage(GX, GY) if a is None else a
    keep = [r for r in range(16) if r not in excluded]
    rows = [a[r] for r in keep]
    columns = {}
    for slot, r in enumerate(keep):
        means = [[1.0 if i == slot else 0.0] * 3 for i in range(len(keep))]
        # No clamping: M has to stay linear in the measurements. The service
        # clamps the reconstructed colour, which is the only place a clamp
        # belongs, because here a negative weight is what keeps the field
        # averaging back to the measurements it came from.
        field = regularised_field(rows, means, lam, clamp=False)
        columns[r] = [field[k][0] for k in range(len(field))]
    zero = [0.0] * len(columns[keep[0]])
    return [[columns[r][k] if r in columns else zero[k] for r in range(16)]
            for k in range(len(zero))]


def emit(path, lam):
    m = build_operator(lam, EXCLUDED_REGIONS)
    peak = max(abs(v) for row in m for v in row)
    scale, bits = (4096, 12) if peak * 4096 < 32000 else (1024, 10)
    out = [
        "/* Generated by tools/histogram_reconstruct.py --emit. Do not edit by hand.",
        " *",
        f" * M maps the sixteen region colours to a {GX}x{GY} cell colour grid:",
        f" *   cell[c] = clamp(sum(region[r][c] * M[cell][r]) >> {bits})",
        " *",
        " * It is the regularised inverse of the measured region geometry,",
        f" *   min |A x - m|^2 + {lam:g} * |L x|^2      (no prior term),",
        " * solved with conjugate gradients and baked here because the TV cannot",
        f" * invert a {GX * GY}x{GX * GY} system, and because M depends only on the",
        " * rectangles, which come from the calibration profile.",
        " *",
        f" * Regions {', '.join(str(r) for r in EXCLUDED_REGIONS)} are excluded: their",
        " * columns are zero, so they reach the grid only through the field the",
        " * other windows imply. They are the two panel-anchored probes, whose",
        " * rectangles are thin and whose means sit apart from the areas around",
        " * them, so including them pulled their neighbourhoods towards themselves",
        " * on the wall. The bar still comes from r14 directly (LAYOUT_BAR_REGION).",
        " */",
        "#ifndef HYPERDRM_RECONSTRUCT_TABLE_H",
        "#define HYPERDRM_RECONSTRUCT_TABLE_H",
        f"#define RECONSTRUCT_GX {GX}",
        f"#define RECONSTRUCT_GY {GY}",
        f"#define RECONSTRUCT_SHIFT {bits}",
        f"#define RECONSTRUCT_PEAK_ENTRY {peak:.4f}f",
        f"static const short reconstruct_m[{GX * GY}][16] = {{",
    ]
    for cell, row in enumerate(m):
        out.append("    {" + ",".join(str(int(round(v * scale))) for v in row) + "},")
    out += ["};", "#endif", ""]
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(out))
    print(f"wrote {path}: {GX}x{GY} cells, shift {bits}, peak entry {peak:.3f}")


if "--emit" in __import__("sys").argv:
    argv = __import__("sys").argv
    emit(argv[argv.index("--emit") + 1],
         float(argv[argv.index("--lambda") + 1]) if "--lambda" in argv else 0.01)


if __name__ == "__main__" and "--emit" not in __import__("sys").argv:
    main()
