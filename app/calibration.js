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
