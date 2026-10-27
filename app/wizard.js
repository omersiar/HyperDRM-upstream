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

function wizardSample() {
    if (!wizard || wizard.busy) return;
    var session = wizard;
    if (Date.now() - wizard.started > 100000) {
        wizardFinish('Calibration timed out. Saved profile unchanged.');
        return;
    }
    if (wizard.phase !== WizardPhase.MEASURING) return;
    var t = video.currentTime;
    if (t >= 68) {
        try {
            var measured = calibrationFit(calibrationAverage(wizard.baseline), wizard.patches.map(calibrationAverage), wizard.model);
            profile = 'custom';
            profileDirty = true;
            showProfile();
            document.getElementById('customProfile').value = JSON.stringify(measured, null, 2);
            document.getElementById('wizardStatus').textContent = 'Measured ' + measured.points.length + ' edge regions. Select Save profile, then Validate profile to check all four colored edges. The previous saved profile is still active.';
            wizard.busy = true;
            wizard.phase = WizardPhase.COMMITTING;
            call('calibrationDraft', function(r) {
                if (wizard === session) wizardFinish(r.returnValue === false ? r.errorText : undefined);
            }, {
                profile: 'custom',
                model: measured.model,
                points: measured.points
            });
        } catch (e) {
            wizardFinish(e.message);
        }
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
                video.currentTime = validation ? 68 : 0;
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
    if (wizard && wizard.validation) wizardFinish('Validation finished. Top-left red, top-right green, bottom-right blue, bottom-left yellow. Check matching LED positions.');
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
// An explicit launch action also supports starting the TV's guided test remotely.
function wizardLaunch(raw) {
    try {
        var params = typeof raw === 'string' ? JSON.parse(raw) : raw;
        if (params && params.action === 'calibrate') wizardStart(false);
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
    wizardLaunch(PalmSystem.launchParams);
}, 1000);
window.addEventListener('webOSRelaunch', function() {
    if (typeof PalmSystem !== 'undefined') wizardLaunch(PalmSystem.launchParams);
});
