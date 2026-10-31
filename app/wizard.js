'use strict';
var WizardPhase = {
    STARTING: 'starting',
    WAITING: 'waiting',
    MEASURING: 'measuring',
    VALIDATING: 'validating',
    COMMITTING: 'committing'
};
var wizard = null;
var video = document.getElementById('calibrationVideo');
var EXTENT_BLACK_START = 68,
    EXTENT_WHITE_START = 70,
    EXTENT_VERTICAL_START = 72,
    EXTENT_HORIZONTAL_START = 90,
    EXTENT_END = 108,
    EXTENT_SPLITS = 9;

function wizardFinish(error) {
    if (!wizard) return;
    clearInterval(wizard.timer);
    wizard = null;
    window.remoteNavigationScope = null;
    video.pause();
    video.style.display = 'none';
    if (typeof video.removeAttribute === 'function') {
        video.removeAttribute('src');
        video.load();
    }
    document.getElementById('cancelCalibration').disabled = true;
    document.getElementById('calibrate').disabled = false;
    document.getElementById('validateCalibration').disabled = false;
    call('calibrationSession', function(r) {
        if (r.returnValue === false) document.getElementById('error').textContent = r.errorText;
        else call('status', display);
    }, {
        enabled: false
    });
    call('calibrationResult', function() {}, {
        message: error || document.getElementById('wizardStatus').textContent
    });
    if (error) document.getElementById('wizardStatus').textContent = error;
    document.getElementById('calibrate').focus();
}

function extentStep(t) {
    if (t < EXTENT_BLACK_START || t >= EXTENT_END) return null;
    if (t < EXTENT_WHITE_START) return {
        kind: 'black',
        index: 0,
        local: t - EXTENT_BLACK_START
    };
    if (t < EXTENT_VERTICAL_START) return {
        kind: 'white',
        index: 0,
        local: t - EXTENT_WHITE_START
    };
    if (t < EXTENT_HORIZONTAL_START) return {
        kind: 'vertical',
        index: Math.floor((t - EXTENT_VERTICAL_START) / 2),
        local: (t - EXTENT_VERTICAL_START) % 2
    };
    return {
        kind: 'horizontal',
        index: Math.floor((t - EXTENT_HORIZONTAL_START) / 2),
        local: (t - EXTENT_HORIZONTAL_START) % 2
    };
}

function wizardExtentSample(t) {
    var extent = extentStep(t);
    if (!extent || extent.local < .6 || extent.local > 1.8) return;
    var session = wizard;
    wizard.busy = true;
    call('status', function(s) {
        if (wizard !== session) return;
        wizard.busy = false;
        if (s.returnValue === false || !s.calibrating) {
            wizardFinish(s.errorText || 'Calibration session ended. Saved profile unchanged.');
            return;
        }
        if (s.sampleSequence === wizard.sequence || s.sampleAgeMs < 0 || s.sampleAgeMs > 350 || !s.colorValid || !Array.isArray(s.blackPercent) || s.blackPercent.length !== 16) return;
        wizard.sequence = s.sampleSequence;
        if (extent.kind === 'black') wizard.blackReference.push(s.blackPercent);
        else if (extent.kind === 'white') wizard.whiteBlack.push(s.blackPercent);
        else if (extent.kind === 'vertical') wizard.vsplit[extent.index].push(s.blackPercent);
        else wizard.hsplit[extent.index].push(s.blackPercent);
        if ((extent.kind === 'black' || extent.kind === 'white') && Array.isArray(s.regions) && s.regions.length === 16) (extent.kind === 'black' ? wizard.blackRgb : wizard.whiteRgb).push(s.regions);
        document.getElementById('wizardStatus').textContent = extent.kind === 'black' ? 'Measuring black reference...' : extent.kind === 'white' ? 'Measuring white reference...' : 'Measuring region extents (' + extent.kind + ' ' + (extent.index + 1) + ' of ' + EXTENT_SPLITS + ')...';
    });
}

/* Build the proposal from the 32 edge patches plus the split sweeps. The edge
 * patches still provide the rim anchors; the split sweeps measure the actual
 * sampling rectangle of every region. */
function wizardCommit() {
    var session = wizard;
    try {
        /* A single missed status reply must not discard a two-minute run. A
         * missing patch step is treated as the black reference (no response);
         * a missing baseline falls back to zero, which is the black level. */
        var baseline = calibrationMean(wizard.baseline),
            patches = wizard.patches.map(function(p) {
                return calibrationMean(p) || baseline;
            });
        if (!baseline) baseline = Array.from({
            length: 16
        }, function() {
            return 0;
        });
        patches = patches.map(function(p) {
            return p || baseline;
        });
        var measured = calibrationFit(baseline, patches, wizard.model),
            rectangles = null,
            blackRgb = calibrationMeanRgb(wizard.blackRgb),
            whiteRgb = calibrationMeanRgb(wizard.whiteRgb);
        try {
            var black = calibrationMean(wizard.blackReference) || calibrationMean(wizard.baselineBlack),
                white = calibrationMean(wizard.whiteBlack);
            if (black && white) rectangles = calibrationExtents(black, white, wizard.vsplit.map(calibrationMean), wizard.hsplit.map(calibrationMean));
        } catch (extentError) {
            rectangles = null;
        }
        var stored = null;
        try {
            stored = JSON.parse(document.getElementById('customProfile').value);
        } catch (e) {
            stored = null;
        }
        measured.compensate = !!(stored && stored.compensate === true);
        /* Keep whatever colour statistic the profile already used; the wizard
         * only measures geometry and references, it never chooses a summary.
         * The list is local so this file does not depend on app.js globals. */
        var stats = ['mean', 'active', 'p50', 'p90', 'peak'];
        measured.colorStat = stored && stats.indexOf(stored.colorStat) >= 0 ? stored.colorStat : 'mean';
        if (blackRgb && whiteRgb) {
            measured.black = blackRgb.map(function(rgb) {
                return rgb.map(function(value) {
                    return Math.round(value);
                });
            });
            measured.white = whiteRgb.map(function(rgb) {
                return rgb.map(function(value) {
                    return Math.round(value);
                });
            });
        }
        if (rectangles && rectangles.length) {
            measured.rectangles = rectangles;
            /* Keep a deliberate rim choice, but use the measured rectangles when
             * the user had selected rectangles or had no explicit geometry. */
            measured.geometry = stored && stored.geometry === 'rim' ? 'rim' : 'rectangles';
        } else if (stored && stored.geometry === 'rectangles') {
            measured.geometry = 'rectangles';
            if (stored.model === measured.model && stored.rectangles && stored.rectangles.length) measured.rectangles = stored.rectangles;
        }
        profile = 'custom';
        profileDirty = true;
        showProfile();
        document.getElementById('customProfile').value = JSON.stringify(measured, null, 2);
        document.getElementById('wizardStatus').textContent = 'Measured ' + measured.points.length + ' edge regions' + (measured.rectangles ? ' and ' + measured.rectangles.length + ' region rectangles' : '') + (measured.black ? ' with black/white references' : '') + '. Select Save profile, then Validate profile to check all four colored edges. The previous saved profile is still active.';
        wizard.busy = true;
        wizard.phase = WizardPhase.COMMITTING;
        call('calibrationDraft', function(r) {
            if (wizard === session) wizardFinish(r.returnValue === false ? r.errorText : undefined);
        }, {
            profile: 'custom',
            model: measured.model,
            points: measured.points,
            geometry: measured.geometry,
            rectangles: measured.rectangles || [],
            black: measured.black,
            white: measured.white,
            compensate: measured.compensate === true,
            colorStat: measured.colorStat
        });
    } catch (e) {
        wizardFinish(e.message);
    }
}

function wizardSample() {
    if (!wizard || wizard.busy) return;
    var session = wizard;
    if (Date.now() - wizard.started > 180000) {
        wizardFinish('Calibration timed out. Saved profile unchanged.');
        return;
    }
    if (wizard.phase !== WizardPhase.MEASURING) return;
    var t = video.currentTime;
    if (t >= EXTENT_END) {
        wizardCommit();
        return;
    }
    if (t >= EXTENT_BLACK_START) {
        wizardExtentSample(t);
        return;
    }
    var step = t < 4 ? -1 : Math.floor((t - 4) / 2),
        local = t < 4 ? t : (t - 4) % 2;
    if (step < 0 && (t < .8 || t > 3.2) || step >= 0 && (local < .65 || local > 1.75)) return;
    wizard.busy = true;
    call('status', function(s) {
        if (wizard !== session) return;
        wizard.busy = false;
        if (s.returnValue === false || !s.calibrating) {
            wizardFinish(s.errorText || 'Calibration session ended. Saved profile unchanged.');
            return;
        }
        // Ignore duplicates, held output and replies crossing a stimulus boundary.
        var current = video.currentTime,
            nowStep = current < 4 ? -1 : Math.floor((current - 4) / 2);
        if (nowStep !== step || s.sampleSequence === wizard.sequence || s.sampleAgeMs < 0 || s.sampleAgeMs > 350 || !s.colorValid || !Array.isArray(s.regions) || s.regions.length !== 16) return;
        wizard.sequence = s.sampleSequence;
        (step < 0 ? wizard.baseline : wizard.patches[step]).push(calibrationLuma(s.regions));
        if (step < 0 && Array.isArray(s.blackPercent) && s.blackPercent.length === 16) wizard.baselineBlack.push(s.blackPercent);
        document.getElementById('wizardStatus').textContent = step < 0 ? 'Measuring black reference...' : 'Measuring edge patch ' + (step + 1) + ' of 32...';
    });
}

function wizardStart(validation) {
    if (typeof versionCompatible !== 'undefined' && !versionCompatible) {
        document.getElementById('wizardStatus').textContent = 'Finish the app/service update before calibration.';
        return;
    }
    if (wizard) return;
    if (typeof showPanel === 'function') showPanel('calibration', false);
    if (targetDirty || profileDirty) {
        document.getElementById('wizardStatus').textContent = 'Save receiver and profile edits before starting.';
        return;
    }
    document.getElementById('calibrate').disabled = true;
    document.getElementById('validateCalibration').disabled = true;
    document.getElementById('cancelCalibration').disabled = false;
    window.remoteNavigationScope = ['cancelCalibration'];
    wizard = {
        phase: WizardPhase.STARTING,
        validation: validation,
        started: Date.now(),
        busy: false,
        sequence: -1,
        model: '',
        baseline: [],
        baselineBlack: [],
        blackReference: [],
        whiteBlack: [],
        blackRgb: [],
        whiteRgb: [],
        vsplit: Array.from({
            length: EXTENT_SPLITS
        }, function() {
            return [];
        }),
        hsplit: Array.from({
            length: EXTENT_SPLITS
        }, function() {
            return [];
        }),
        patches: Array.from({
            length: 32
        }, function() {
            return [];
        })
    };
    var session = wizard;
    document.getElementById('wizardStatus').textContent = 'Starting full-screen capture. Keep the app visible; do not change zoom or aspect ratio.';
    call('calibrationSession', function(r) {
        if (wizard !== session) return;
        if (r.returnValue === false) {
            wizardFinish(r.errorText);
            return;
        }
        // Start the video plane before waiting for histogram data. Without another
        // running video, waiting first would prevent calibration from ever starting.
        video.src = 'calibration.mp4';
        if (typeof video.load === 'function') video.load();
        video.style.display = 'block';
        var warmup = video.play();
        if (warmup && warmup.catch) warmup.catch(function() {
            if (wizard !== session) return;
            wizardFinish('The TV could not play the calibration video. Saved profile unchanged.');
        });
        wizard.phase = WizardPhase.WAITING;
        call('start', function() {
            if (wizard === session) waitCapture();
        });
    }, {
        enabled: true
    });

    function waitCapture() {
        if (wizard !== session) return;
        call('status', function(s) {
            if (wizard !== session) return;
            if (s.running && s.activeSource !== 'PicCap' && s.tvModel && s.colorValid) {
                wizard.phase = validation ? WizardPhase.VALIDATING : WizardPhase.MEASURING;
                wizard.model = s.tvModel;
                video.currentTime = validation ? EXTENT_END : 0;
                video.style.display = 'block';
                document.getElementById('cancelCalibration').focus();
                var playing = video.play();
                if (playing && playing.catch) playing.catch(function() {
                    if (wizard !== session) return;
                    wizardFinish('The TV could not play the calibration video. Saved profile unchanged.');
                });
                wizard.timer = setInterval(wizardSample, 250);
            } else if (Date.now() - wizard.started < 15000) setTimeout(waitCapture, 500);
            else wizardFinish('No fresh full-screen histogram capture or TV model. Saved profile unchanged.');
        });
    }
}
document.getElementById('calibrate').onclick = function() {
    wizardStart(false);
};
document.getElementById('validateCalibration').onclick = function() {
    wizardStart(true);
};
document.getElementById('cancelCalibration').onclick = function() {
    wizardFinish('Calibration cancelled. Saved profile unchanged.');
};
video.addEventListener('ended', function() {
    if (!wizard) {
        /* Pattern playback: do not leave a full-screen frame behind. */
        video.style.display = 'none';
        if (typeof video.removeAttribute === 'function') {
            video.removeAttribute('src');
            video.load();
        }
        return;
    }
    if (wizard.validation) wizardFinish('Validation finished. Top-left red, top-right green, bottom-right blue, bottom-left yellow. Check matching LED positions.');
    else wizardSample();
});
video.addEventListener('error', function() {
    wizardFinish('Calibration video playback failed. Saved profile unchanged.');
});
document.addEventListener('keydown', function(e) {
    if (wizard && (e.keyCode === 461 || e.key === 'Escape')) {
        e.preventDefault();
        wizardFinish('Calibration cancelled. Saved profile unchanged.');
    }
});
window.addEventListener('pagehide', function() {
    wizardFinish();
});
/* Diagnostic pattern playback.
 *
 * Launched with {"action":"play","src":"pattern.mp4"}, this puts a generated
 * stimulus on the video plane without the measurement wizard: no calibration
 * session, no sampling timer and, because #cancelCalibration stays disabled,
 * no overlay above the video. The file name is restricted to a plain name in
 * the app directory so a launch parameter cannot reach another path. */
var patternSource = null;
function wizardPlay(name) {
    if (wizard) return;
    var file = typeof name === 'string' ? name : 'pattern.mp4';
    if (!/^[A-Za-z0-9._-]+\.mp4$/.test(file)) {
        document.getElementById('wizardStatus').textContent = 'Invalid pattern video name.';
        return;
    }
    if (patternSource === file && video.style.display === 'block' && !video.paused) return;
    patternSource = file;
    video.src = file;
    if (typeof video.load === 'function') video.load();
    video.style.display = 'block';
    var started = video.play();
    if (started && typeof started.catch === 'function') started.catch(function() {});
}
/* webOS versions disagree on where launch parameters arrive: PalmSystem, then
 * webOSSystem, and newer builds only carry them on the launch/relaunch events.
 * The guided test is documented as remotely startable, so read all of them. */
function wizardLaunchParams(event) {
    if (event && event.detail) {
        if (typeof event.detail === 'string') return event.detail;
        if (event.detail.launchParams) return event.detail.launchParams;
        if (event.detail.params) return event.detail.params;
    }
    if (typeof PalmSystem !== 'undefined' && PalmSystem.launchParams) return PalmSystem.launchParams;
    if (typeof webOSSystem !== 'undefined' && webOSSystem.launchParams) return webOSSystem.launchParams;
    return null;
}
// An explicit launch action also supports starting the TV's guided test remotely.
function wizardLaunch(raw) {
    try {
        var params = typeof raw === 'string' ? JSON.parse(raw) : raw;
        if (params && params.action === 'calibrate') wizardStart(false);
        else if (params && params.action === 'play') wizardPlay(params.src);
        else if (params && params.action === 'repairRoot') {
            showPanel('overview');
            repairRoot(function(ok) {
                if (ok) call('status', display);
            });
        }
    } catch (e) {
        document.getElementById('wizardStatus').textContent = 'Invalid launch parameters.';
    }
}
if (typeof PalmSystem !== 'undefined' && PalmSystem.launchParams) setTimeout(function() {
    wizardLaunch(wizardLaunchParams(null));
}, 1000);
window.addEventListener('webOSLaunch', function(e) {
    var raw = wizardLaunchParams(e);
    if (raw) wizardLaunch(raw);
});
window.addEventListener('webOSRelaunch', function(e) {
    var raw = wizardLaunchParams(e);
    if (raw) wizardLaunch(raw);
});
