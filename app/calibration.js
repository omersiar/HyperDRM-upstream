/* Pure measurement processing; the video uses 32 clockwise edge patches. */
function calibrationFit(baseline, patches, model) {
    if (!model || baseline.length !== 16 || patches.length !== 32) throw new Error('Incomplete calibration measurements.');
    var candidates = [],
        edges = [0, 0, 0, 0];
    for (var region = 0; region < 16; region++) {
        var responses = patches.map(function(p) {
            if (!p || p.length !== 16) throw new Error('Missing video measurements.');
            return Math.max(0, p[region] - baseline[region]);
        });
        var peak = Math.max.apply(Math, responses),
            best = responses.indexOf(peak);
        if (peak < 8) continue;
        // Reject regions responding equally around the screen (global histograms).
        var strong = responses.filter(function(v) {
            return v >= peak * .65;
        }).length;
        if (strong > 10) continue;
        var distant = responses.some(function(v, i) {
            var d = Math.min(Math.abs(i - best), 32 - Math.abs(i - best));
            return v >= peak * .65 && d > 6;
        });
        if (distant) continue;
        var sum = 0,
            weighted = 0;
        for (var offset = -3; offset <= 3; offset++) {
            var index = (best + offset + 32) % 32,
                weight = Math.max(0, responses[index] - peak * .35);
            weighted += offset * weight;
            sum += weight;
        }
        var position = ((best + .5 + weighted / sum) / 8 + 4) % 4;
        candidates.push({
            position: position,
            region: region,
            score: peak / strong
        });
    }
    // Nested LG regions can measure the same edge position. Keep the most
    // localized response rather than making duplicate interpolation anchors.
    candidates.sort(function(a, b) {
        return b.score - a.score || a.region - b.region;
    });
    var points = [];
    candidates.forEach(function(p) {
        if (points.some(function(q) {
                var d = Math.abs(p.position - q.position);
                return Math.min(d, 4 - d) < .06;
            })) return;
        points.push({
            position: p.position,
            region: p.region
        });
        edges[Math.floor(p.position)]++;
    });
    points.sort(function(a, b) {
        return a.position - b.position;
    });
    if (points.length < 4 || edges.some(function(n) {
            return !n;
        })) throw new Error('No reliable spatial response on every edge. Keep the video full-screen; this TV may not expose local histogram regions. Your saved profile is unchanged.');
    return {
        model: model,
        points: points
    };
}

function calibrationLuma(regions) {
    return regions.map(function(c) {
        return (c[0] + c[1] + c[2]) / 3;
    });
}

function calibrationAverage(samples) {
    if (samples.length < 3) throw new Error('Not enough fresh histogram samples. Keep the app visible and repeat calibration.');
    return samples[0].map(function(_, r) {
        return samples.reduce(function(sum, s) {
            return sum + s[r];
        }, 0) / samples.length;
    });
}

/* Optional mean for the extent sweeps: a single missed step must not discard
 * the whole measurement, so empty sets return null instead of throwing. */
function calibrationMean(samples) {
    if (!samples || !samples.length) return null;
    return samples[0].map(function(_, r) {
        return samples.reduce(function(sum, s) {
            return sum + s[r];
        }, 0) / samples.length;
    });
}

/* Mean of a 16x3 RGB matrix, used for the black/white reference phases. */
function calibrationMeanRgb(samples) {
    if (!samples || !samples.length) return null;
    return samples[0].map(function(_, r) {
        return samples[0][r].map(function(_, c) {
            return samples.reduce(function(sum, s) {
                return sum + s[r][c];
            }, 0) / samples.length;
        });
    });
}

/* Fit f(s) = clamp((s-x0)/(x1-x0), 0, 1) with 0 <= x0 < x1 <= 1. The black
 * share of a white/black split is an exact area fraction, so the two edges are
 * recovered without gamma assumptions. The bounded grid also handles regions
 * whose response saturates before the tenth or after the ninetieth split. */
function calibrationExtentFit(fractions) {
    var best = null;
    for (var a = 0; a <= 100; a++) {
        for (var w = 1; w <= 100 - a; w++) {
            var x0 = a / 100,
                x1 = (a + w) / 100,
                sum = 0,
                count = 0;
            for (var i = 0; i < fractions.length; i++) {
                var f = fractions[i];
                if (f === null) continue;
                var s = (i + 1) / 10,
                    expected = s <= x0 ? 0 : s >= x1 ? 1 : (s - x0) / (x1 - x0),
                    delta = f - expected;
                sum += delta * delta;
                count++;
            }
            if (!count) return null;
            var rmse = Math.sqrt(sum / count);
            if (!best || rmse < best.rmse) best = {
                x0: x0,
                x1: x1,
                rmse: rmse
            };
        }
    }
    return best;
}

function calibrationFractions(black, contrast, region, sets) {
    return sets.map(function(set) {
        if (!set) return null;
        return Math.max(0, Math.min(1, (black[region] - set[region]) / contrast));
    });
}

/* Recover the actual sampling rectangle of each LG histogram region from nine
 * vertical and nine horizontal white/black splits. black and white are the
 * per-region black percentages measured on a full-black and a full-white
 * frame; vertical[k]/horizontal[k] are the per-region black percentages at
 * split position (k+1)/10. Returns 4-16 rectangles. */
function calibrationExtents(black, white, vertical, horizontal) {
    if (!black || !white || black.length !== 16 || white.length !== 16 || !vertical || !horizontal || vertical.length !== 9 || horizontal.length !== 9) throw new Error('Incomplete extent measurements. Keep the video full-screen and repeat calibration.');
    var rectangles = [];
    for (var region = 0; region < 16; region++) {
        var contrast = black[region] - white[region];
        if (!(contrast > 10)) continue;
        var xFit = calibrationExtentFit(calibrationFractions(black, contrast, region, vertical)),
            yFit = calibrationExtentFit(calibrationFractions(black, contrast, region, horizontal));
        if (!xFit || !yFit || xFit.rmse > .12 || yFit.rmse > .12) continue;
        if (xFit.x1 - xFit.x0 < .02 || yFit.x1 - yFit.x0 < .02) continue;
        rectangles.push({
            region: region,
            x0: xFit.x0,
            y0: yFit.x0,
            x1: xFit.x1,
            y1: yFit.x1
        });
    }
    if (rectangles.length < 4) throw new Error('No reliable region extents on this TV. Keep the perimeter profile.');
    return rectangles;
}
