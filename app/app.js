'use strict';
var pending = [];
var actionError = '';
var versionCompatible = typeof HYPERDRM_APP_VERSION === 'undefined';

function checkServiceVersion(result) {
    if (typeof HYPERDRM_APP_VERSION === 'undefined' || result.returnValue === false) return;
    versionCompatible = result.version === HYPERDRM_APP_VERSION;
    var warning = document.getElementById('versionWarning');
    warning.style.display = versionCompatible ? 'none' : 'block';
    document.getElementById('reloadService').style.display = versionCompatible ? 'none' : 'inline-block';
    warning.textContent = versionCompatible ? '' : 'App ' + HYPERDRM_APP_VERSION + ' / service ' + (result.version || 'unknown') + '. The installed service does not match this app. Finish the update, then select Reload service before changing settings. Reloading briefly stops ambient lighting.';
    ['start', 'autostart', 'handover', 'saveTarget', 'testConnection', 'calibrate', 'validateCalibration', 'saveProfile', 'compensate', 'colorStat'].forEach(function(id) {
        var button = document.getElementById(id);
        if (!versionCompatible) {
            button.versionBlocked = true;
            button.disabled = true;
        } else if (button.versionBlocked) {
            button.versionBlocked = false;
            button.disabled = id === 'handover' ? !result.piccapInstalled : id === 'testConnection' && result.connectionTest && ['Connecting', 'Sending request', 'Checking authentication', 'Reading LED layout'].indexOf(result.connectionTest.state) >= 0;
        }
    });
}

function showActionError(message) {
    actionError = message || '';
    document.getElementById('error').textContent = actionError;
}

function clearActionError() {
    showActionError('');
}
var targetDirty = false,
    receiver = 'Hyperion',
    output = 'json';
var profileDirty = false,
    profile = 'auto',
    geometry = 'rim',
    compensate = false,
    colorStat = 'mean',
    grade = 'off',
    advancedProfile = false;
/* How a region's pixels become one value. Ordered as the button cycles; the
 * names match the profile's colorStat key and the service's status reply. */
var colorStatNames = {mean: 'Mean', active: 'Active', p50: 'p50', p90: 'p90', peak: 'Peak', vivid: 'Vivid'};
/* Vivid sits next to Mean: they are the two everyday choices, and the
 * percentiles are the ones you go looking for. */
var colorStatOrder = ['mean', 'vivid', 'active', 'p50', 'p90', 'peak'];
/* Brightness and colour grading, applied to the finished region colours. */
var gradeNames = {off: 'Off', gentle: 'Gentle', strong: 'Strong'};
var gradeOrder = ['off', 'gentle', 'strong'];
var profileNames = {
    auto: 'Auto',
    g4: 'LG G4 (measured)',
    average: 'Average (uniform)',
    custom: 'Custom model'
};
var geometryNames = {
    rim: 'Perimeter',
    rectangles: 'Region rectangles',
    smooth: 'Reconstructed'
};
/* Order the geometry button cycles through. "smooth" needs the same measured
 * rectangles as "rectangles"; it only changes how the plane is filled. */
var geometryOrder = ['rim', 'rectangles', 'smooth'];

function showCompensate() {
    var button = document.getElementById('compensate');
    if (!button) return;
    button.textContent = 'Colour cast compensation: ' + (compensate ? 'On' : 'Off');
    button.disabled = profile !== 'custom';
}

function showColorStat() {
    var button = document.getElementById('colorStat');
    if (!button) return;
    button.textContent = 'Colour: ' + (colorStatNames[colorStat] || 'Mean');
    button.disabled = profile !== 'custom';
}

function showGrade() {
    var button = document.getElementById('grade');
    if (!button) return;
    button.textContent = 'Brightness and colour: ' + (gradeNames[grade] || 'Off');
    button.disabled = profile !== 'custom';
}

function showProfile() {
    document.getElementById('profile').textContent = 'Profile: ' + profileNames[profile];
    document.getElementById('geometry').textContent = 'Geometry: ' + geometryNames[geometry];
    document.getElementById('geometry').disabled = profile !== 'custom';
    showCompensate();
    showColorStat();
    showGrade();
    var visible = profile === 'custom' && advancedProfile;
    document.getElementById('customEditor').style.display = visible ? 'block' : 'none';
    document.getElementById('customProfile').disabled = !visible;
    document.getElementById('editProfile').disabled = profile !== 'custom';
    document.getElementById('editProfile').textContent = visible ? 'Hide profile JSON' : 'Edit profile JSON';
}

function call(method, done, data) {
    if (!versionCompatible && ['start', 'autostart', 'handover', 'settings', 'testConnection', 'calibration', 'calibrationSession', 'calibrationDraft'].indexOf(method) >= 0 && !(method === 'calibrationSession' && data && data.enabled === false)) {
        done({
            returnValue: false,
            errorText: 'App and service versions do not match. Finish the update first.'
        });
        return;
    }
    if (['start', 'stop', 'autostart', 'handover', 'settings', 'testConnection', 'calibration'].indexOf(method) >= 0) clearActionError();
    callUri('luna://org.webosbrew.histogramhyperion.service/' + method, function(result) {
        if (method !== 'status' && result.returnValue === false) showActionError(result.errorText || 'The action failed.');
        done(result);
    }, data);
}

function callUri(uri, done, data) {
    if (typeof PalmServiceBridge === 'undefined') {
        done({
            returnValue: false,
            errorText: 'This app requires an LG TV with the native service installed.'
        });
        return;
    }
    var bridge = new PalmServiceBridge(),
        timer, finished = false;

    function finish(value) {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        var i = pending.indexOf(bridge);
        if (i >= 0) pending.splice(i, 1);
        done(value);
    }
    bridge.onservicecallback = function(raw) {
        try {
            finish(JSON.parse(raw));
        } catch (e) {
            finish({
                returnValue: false,
                errorText: 'Invalid service response'
            });
        }
    };
    pending.push(bridge);
    timer = setTimeout(function() {
        bridge.cancel();
        finish({
            returnValue: false,
            errorText: 'The service is not responding. Check the installation and service log.'
        });
    }, uri.indexOf('hbchannel') >= 0 ? 30000 : 10000);
    bridge.call(uri, JSON.stringify(data || {}));
}

function display(result) {
    if (typeof result.uid === 'number') {
        document.getElementById('grantRoot').style.display = result.uid === 0 ? 'none' : 'inline-block';
        document.getElementById('rootHint').style.display = result.uid === 0 ? 'none' : 'block';
    }
    if (result.calibrationProfile && !profileDirty) {
        profile = result.calibrationProfile;
        geometry = geometryNames[result.customGeometry] ? result.customGeometry : 'rim';
        showProfile();
        var stored = {
            model: result.customModel || result.tvModel || 'TV-MODEL',
            points: result.customPoints && result.customPoints.length ? result.customPoints : []
        };
        if (result.customGeometry === 'rectangles' || result.customGeometry === 'smooth') stored.geometry = result.customGeometry;
        else if (result.customRectangles && result.customRectangles.length) stored.geometry = 'rim';
        if (result.customRectangles && result.customRectangles.length) stored.rectangles = result.customRectangles;
        compensate = !!result.customCompensate;
        colorStat = colorStatNames[result.colorStat] ? result.colorStat : 'mean';
        grade = gradeNames[result.grade] ? result.grade : 'off';
        stored.compensate = compensate;
        stored.colorStat = colorStat;
        if (grade !== 'off') stored.grade = grade;
        if (result.customBlack && result.customBlack.length) stored.black = result.customBlack;
        if (result.customWhite && result.customWhite.length) stored.white = result.customWhite;
        document.getElementById('customProfile').value = JSON.stringify(stored, null, 2);
    }
    showCompensate();
    showColorStat();
    showGrade();
    if (result.effectiveCalibration) document.getElementById('profileStatus').textContent = 'TV: ' + (result.tvModel || 'Unknown') + ' · Active profile: ' + profileNames[result.effectiveCalibration] + (result.effectiveCalibration === 'custom' ? ' · ' + (geometryNames[result.spatialGeometry] || geometryNames.rim) : '') + (result.effectiveCalibration === 'average' ? ' · Spatial calibration needed for this model' : '');

    if (result.host && !targetDirty) {
        document.getElementById('host').value = result.host;
        document.getElementById('port').value = result.port;
        document.getElementById('piccapPort').value = result.piccapPort || 19400;
        document.getElementById('captureInterval').value = result.captureIntervalMs || 50;
        receiver = result.receiver;
        document.getElementById('receiver').textContent = 'Receiver: ' + receiver;
        output = result.output || 'json';
        document.getElementById('output').textContent = 'Output: ' + (output === 'flatbuffer' ? 'FlatBuffer, all instances' : 'JSON, one instance');
    }
    document.getElementById('token').placeholder = result.tokenSet ? 'Saved token (leave blank to keep)' : 'No token saved';
    if (result.endpoint) document.getElementById('endpoint').textContent = result.receiver + ' · ' + result.endpoint + ' · Priority 149' + (result.captureIntervalMs ? ' · ' + result.captureIntervalMs + ' ms capture' : '');
    var t = result.connectionTest;
    if (t) document.getElementById('testResult').textContent = 'Connection test: ' + t.state + (t.state === 'Passed' ? ' · ' + t.ledCount + ' LEDs · ' + t.elapsedMs + ' ms' : '') + (t.error ? ' · ' + t.error : '');
    document.getElementById('testConnection').disabled = !!t && ['Connecting', 'Sending request', 'Checking authentication', 'Reading LED layout'].indexOf(t.state) >= 0;
    if (result.version) document.getElementById('diagnostics').textContent = 'Version ' + result.version + ' · ' + (result.uid === 0 ? 'Root service' : 'Service needs root elevation') + ' · ' + result.captureState + ' · Capture recoveries: ' + result.captureRecoveries + ' (last: ' + result.lastRecoveryMs + ' ms)' + (result.lastOutputAgeMs >= 0 ? ' · Last histogram output: ' + result.lastOutputAgeMs + ' ms ago' : '') + (result.retryScheduled ? ' · Retry in ' + result.retryInMs + ' ms · Attempts: ' + result.retryAttempts : '') + (result.piccapError ? ' · ' + result.piccapError : '') + (result.piccapCooldownMs ? ' · PicCap retry in ' + Math.ceil(result.piccapCooldownMs / 1000) + ' s' : '') + ' · Layout updates: ' + (result.layoutUpdates || 0) + (result.layoutError ? ' · Layout: ' + result.layoutError : '');
    var handover = document.getElementById('handover');
    handover.disabled = !result.piccapInstalled;
    handover.setAttribute('aria-checked', String(!!result.piccapHandover));
    handover.textContent = result.piccapInstalled ? 'Auto PicCap: ' + (result.piccapHandover ? 'On' : 'Off') : 'Auto PicCap: Not installed';
    if (typeof result.autostart === 'boolean') {
        var toggle = document.getElementById('autostart');
        toggle.setAttribute('aria-checked', String(result.autostart));
        toggle.textContent = 'Autostart: ' + (result.autostart ? 'On' : 'Off');
    }
    document.getElementById('error').textContent = actionError || result.errorText || result.error || '';
    document.getElementById('status').textContent = result.returnValue === false ? 'Service unavailable' : (result.receiverConnecting ? 'Connecting to receiver' : result.running ? 'Running' : 'Stopped') + ' · ' + (result.frames || 0) + ' acknowledged color updates';
    if (result.activeSource === 'PicCap') {
        document.getElementById('status').textContent = 'PicCap · ' + (result.running ? 'Running' : 'Switching capture source') + ' · ' + (result.piccapConnected ? 'Receiver connected' : 'Waiting for receiver');
        document.getElementById('regions').textContent = '';
        checkServiceVersion(result);
        return;
    }
    if (result.piccapHandover) document.getElementById('status').textContent += result.captureHold ? ' · ' + result.captureHold + ' has the capture' : ' · HyperDRM stands in during protected playback';
    if (result.piccapHandover && !result.piccapTargetCompatible) document.getElementById('status').textContent += ' · Auto PicCap unavailable for remote IPv6 target';
    if (result.colorValid && result.effectiveCalibration === 'average') document.getElementById('status').textContent += ' · Average color output';
    if (result.colorValid) document.getElementById('status').textContent += result.spatialOutput ? ' · ' + result.ledCount + ' LEDs from 16 color regions' + (result.receiver === 'HyperHDR' ? ' · Spatial image output' : '') : ' · Output RGB: ' + result.outputColor.join(', ');
    if (result.output === 'flatbuffer') document.getElementById('status').textContent += result.flatbufferConnected ? ' · FlatBuffer connected, every LED instance' : ' · FlatBuffer connecting';
    else if (result.returnValue !== false) document.getElementById('status').textContent += ' · No current color output';
    if (result.blackBarDetection && result.running) document.getElementById('status').textContent += result.letterboxDetected ? ' · Top/bottom black bars detected' : ' · Full-screen capture';
    if (result.spatialInterpolation) document.getElementById('status').textContent += ' · Smooth color transitions';
    if (result.spatialGeometry === 'rectangles') document.getElementById('status').textContent += ' · Region rectangles (reconstructed)';
    if (result.holdingColors) document.getElementById('status').textContent += ' · Holding last valid colors (missing data)';
    if (result.sourceFade) document.getElementById('status').textContent += ' · Blending capture sources';
    if (result.retryScheduled) document.getElementById('status').textContent += ' · Retrying receiver or capture startup';
    if (result.retryPaused) document.getElementById('status').textContent += ' · Automatic retry paused until Start';
    var box = document.getElementById('regions');
    box.textContent = '';
    (result.regions || []).forEach(function(rgb, index) {
        var e = document.createElement('div');
        e.className = 'swatch';
        e.style.backgroundColor = 'rgb(' + rgb.join(',') + ')';
        e.title = 'Histogram ' + index;
        box.appendChild(e);
    });
    checkServiceVersion(result);
}
document.getElementById('reloadService').onclick = function() {
    var button = this;
    button.disabled = true;
    clearActionError();
    call('restartService', function(result) {
        if (result.returnValue === false) {
            button.disabled = false;
            showActionError(result.errorText || 'Reinstall the current package, then restart its service.');
            return;
        }
        setTimeout(function() {
            button.disabled = false;
            call('status', display);
        }, 1500);
    });
};

document.getElementById('start').onclick = function() {
    call('start', function(r) {
        if (r.returnValue === false) display(r);
        else call('status', display);
    });
};
document.getElementById('stop').onclick = function() {
    call('stop', function() {
        call('status', display);
    });
};
document.getElementById('refresh').onclick = function() {
    call('status', display);
};
call('status', display);
setInterval(function() {
    if (!pending.length) call('status', display);
}, 3000);
document.getElementById('start').focus();

var repairingRoot = false,
    rootWaiters = [];

function repairRoot(done) {
    clearActionError();
    rootWaiters.push(done);
    if (repairingRoot) return;
    repairingRoot = true;
    document.getElementById('grantRoot').disabled = true;
    showActionError('Granting root access to HyperDRM...');

    function finish(error) {
        repairingRoot = false;
        document.getElementById('grantRoot').disabled = false;
        if (error) showActionError(error);
        var waiters = rootWaiters;
        rootWaiters = [];
        waiters.forEach(function(callback) {
            callback(!error);
        });
    }
    callUri('luna://org.webosbrew.hbchannel.service/elevateService', function(r) {
        if (r.returnValue === false) {
            finish('Homebrew Channel could not grant root access: ' + (r.errorText || 'Check Homebrew Channel installation.'));
            return;
        }
        call('restartService', function(r) {
            if (r.returnValue === false) {
                finish(r.errorText || 'Could not restart the HyperDRM service.');
                return;
            }
            var attempts = 0;

            function check() {
                call('status', function(s) {
                    if (s.uid === 0) {
                        display(s);
                        finish();
                    } else if (++attempts < 12) setTimeout(check, 1000);
                    else finish('The service is still missing root access. Elevate HyperDRM in Homebrew Channel and reopen the app.');
                });
            }
            setTimeout(check, 1500);
        });
    }, {
        id: 'org.webosbrew.histogramhyperion.service'
    });
}
document.getElementById('grantRoot').onclick = function() {
    repairRoot(function(ok) {
        if (ok) call('status', display);
    });
};
document.getElementById('autostart').onclick = function() {
    var button = this,
        enabled = button.getAttribute('aria-checked') !== 'true';
    button.disabled = true;

    function save() {
        call('autostart', function(r) {
            if (r.rootRequired) {
                repairRoot(function(ok) {
                    if (ok) save();
                    else button.disabled = false;
                });
                return;
            }
            button.disabled = false;
            if (r.returnValue === false) showActionError(r.errorText);
            else call('status', display);
        }, {
            enabled: enabled
        });
    }
    save();
};

document.getElementById('handover').onclick = function() {
    var button = this;
    button.disabled = true;
    call('handover', function(r) {
        button.disabled = false;
        if (r.returnValue === false) showActionError(r.errorText);
        else call('status', display);
    }, {
        enabled: button.getAttribute('aria-checked') !== 'true'
    });
};
['host', 'port', 'piccapPort', 'captureInterval', 'token', 'clearToken'].forEach(function(id) {
    document.getElementById(id).addEventListener('input', function() {
        targetDirty = true;
    });
});
document.getElementById('receiver').onclick = function() {
    receiver = receiver === 'Hyperion' ? 'HyperHDR' : 'Hyperion';
    this.textContent = 'Receiver: ' + receiver;
    targetDirty = true;
};
document.getElementById('output').onclick = function() {
    output = output === 'flatbuffer' ? 'json' : 'flatbuffer';
    this.textContent = 'Output: ' + (output === 'flatbuffer' ? 'FlatBuffer, all instances' : 'JSON, one instance');
    targetDirty = true;
};
document.getElementById('saveTarget').onclick = function() {
    var port = Number(document.getElementById('port').value),
        button = this;
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        showActionError('JSON port must be an integer from 1 to 65535.');
        return;
    }
    var flatPort = Number(document.getElementById('piccapPort').value);
    if (!Number.isInteger(flatPort) || flatPort < 1 || flatPort > 65535) {
        showActionError('PicCap FlatBuffer port must be an integer from 1 to 65535.');
        return;
    }
    var interval = Number(document.getElementById('captureInterval').value);
    if (!Number.isInteger(interval) || interval < 25 || interval > 1000 || interval % 25) {
        showActionError('Capture interval must be 25–1000 ms in steps of 25.');
        return;
    }
    var settings = {
        captureIntervalMs: interval,
        piccapPort: flatPort,
        host: document.getElementById('host').value.trim(),
        port: port,
        receiver: receiver,
        output: output,
        clearToken: document.getElementById('clearToken').checked
    };
    var token = document.getElementById('token').value.trim();
    if (token) settings.token = token;
    button.disabled = true;
    call('settings', function(r) {
        button.disabled = false;
        if (r.returnValue === false) {
            showActionError(r.errorText);
            return;
        }
        targetDirty = false;
        document.getElementById('token').value = '';
        document.getElementById('clearToken').checked = false;
        call('status', display);
    }, settings);
};
document.getElementById('testConnection').onclick = function() {
    if (targetDirty) {
        showActionError('Save receiver settings before testing the connection.');
        return;
    }
    this.disabled = true;
    call('testConnection', function(r) {
        if (r.returnValue === false) showActionError(r.errorText);
        call('status', display);
    });
};
document.getElementById('profile').onclick = function() {
    var keys = ['auto', 'g4', 'average', 'custom'];
    profile = keys[(keys.indexOf(profile) + 1) % keys.length];
    profileDirty = true;
    showProfile();
};
document.getElementById('customProfile').addEventListener('input', function() {
    profileDirty = true;
});
document.getElementById('saveProfile').onclick = function() {
    var data = {
            profile: profile
        },
        button = this;
    if (profile === 'custom') {
        try {
            var parsed = JSON.parse(document.getElementById('customProfile').value);
            data.model = parsed.model;
            data.points = parsed.points;
            data.geometry = geometryNames[parsed.geometry] ? parsed.geometry : geometry;
            if (parsed.rectangles && parsed.rectangles.length) data.rectangles = parsed.rectangles;
            if (parsed.black && parsed.white) {
                data.black = parsed.black;
                data.white = parsed.white;
            }
            data.compensate = parsed.compensate === true || compensate;
            data.colorStat = colorStatNames[parsed.colorStat] ? parsed.colorStat : colorStat;
            data.grade = gradeNames[parsed.grade] ? parsed.grade : grade;
        } catch (e) {
            showActionError('Custom profile must be valid JSON.');
            return;
        }
    }
    button.disabled = true;
    call('calibration', function(r) {
        button.disabled = false;
        if (r.returnValue === false) {
            showActionError(r.errorText);
            return;
        }
        profileDirty = false;
        call('status', display);
    }, data);
};
document.getElementById('editProfile').onclick = function() {
    advancedProfile = !advancedProfile;
    showProfile();
    if (advancedProfile) document.getElementById('customProfileField').focus();
};
document.getElementById('geometry').onclick = function() {
    geometry = geometryOrder[(geometryOrder.indexOf(geometry) + 1) % geometryOrder.length];
    profileDirty = true;
    var field = document.getElementById('customProfile'),
        stored = null;
    try {
        stored = JSON.parse(field.value);
    } catch (e) {
        stored = null;
    }
    if (stored && typeof stored === 'object') {
        stored.geometry = geometry;
        field.value = JSON.stringify(stored, null, 2);
    }
    showProfile();
};
var compensateButton = document.getElementById('compensate');
if (compensateButton) compensateButton.onclick = function() {
    compensate = !compensate;
    profileDirty = true;
    var field = document.getElementById('customProfile'),
        stored = null;
    try {
        stored = JSON.parse(field.value);
    } catch (e) {
        stored = null;
    }
    if (stored && typeof stored === 'object') {
        stored.compensate = compensate;
        field.value = JSON.stringify(stored, null, 2);
    }
    showProfile();
};
var colorStatButton = document.getElementById('colorStat');
if (colorStatButton) colorStatButton.onclick = function() {
    colorStat = colorStatOrder[(colorStatOrder.indexOf(colorStat) + 1) % colorStatOrder.length];
    profileDirty = true;
    var field = document.getElementById('customProfile'),
        stored = null;
    try {
        stored = JSON.parse(field.value);
    } catch (e) {
        stored = null;
    }
    if (stored && typeof stored === 'object') {
        stored.colorStat = colorStat;
        field.value = JSON.stringify(stored, null, 2);
    }
    showProfile();
};
var gradeButton = document.getElementById('grade');
if (gradeButton) gradeButton.onclick = function() {
    grade = gradeOrder[(gradeOrder.indexOf(grade) + 1) % gradeOrder.length];
    profileDirty = true;
    var field = document.getElementById('customProfile'),
        stored = null;
    try {
        stored = JSON.parse(field.value);
    } catch (e) {
        stored = null;
    }
    if (stored && typeof stored === 'object') {
        if (grade === 'off') delete stored.grade;
        else stored.grade = grade;
        field.value = JSON.stringify(stored, null, 2);
    }
    showGrade();
};
/* The remote walks the controls in the order they are drawn, so the list is
 * taken from the document instead of a hand-kept array. A control added to the
 * markup joins the cycle in its visual position, help buttons included; hidden
 * panels, disabled controls and the closed help modal are skipped while a key is
 * pressed. Keeping this list by hand is what made the remote jump out of order
 * whenever a control moved. */
setupRemoteNavigation(Array.prototype.slice.call(
        document.querySelectorAll('button, .field, input[type="checkbox"]')), document, window);
