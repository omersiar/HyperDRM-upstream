function showPanel(name, focus) {
    ['overview', 'receiver', 'calibration'].forEach(function(key) {
        document.getElementById(key + 'Panel').style.display = key === name ? 'block' : 'none';
        document.getElementById(key + 'Tab').setAttribute('aria-pressed', String(key === name));
    });
    if (focus !== false) document.getElementById({
        overview: 'start',
        receiver: 'receiver',
        calibration: 'calibrate'
    } [name]).focus();
}
['overview', 'receiver', 'calibration'].forEach(function(name) {
    document.getElementById(name + 'Tab').onclick = function() {
        showPanel(name);
    };
});
document.getElementById('calibrationShortcut').onclick = function() {
    showPanel('calibration');
};

function setupFormEditors(ids, doc, win) {
    var pairs = ids.map(function(id) {
        return {
            input: doc.getElementById(id),
            field: doc.getElementById(id + 'Field')
        };
    });
    pairs.forEach(function(pair) {
        var input = pair.input,
            field = pair.field;
        input.readOnly = true;
        field.onclick = function() {
            if (input.disabled || !input.readOnly) return;
            input.readOnly = false;
            input.focus();
        };
        input.addEventListener('blur', function() {
            input.readOnly = true;
        });
        input.addEventListener('mousedown', function(event) {
            if (input.readOnly) event.preventDefault();
        });
    });

    function finishEditing() {
        pairs.forEach(function(pair) {
            if (!pair.input.readOnly) {
                pair.input.readOnly = true;
                pair.input.blur();
                pair.field.focus();
            }
        });
    }
    doc.addEventListener('keyboardStateChange', function(event) {
        var detail = event.detail || {};
        if (detail.visibility === false || detail.isShowing === false) finishEditing();
    });
    doc.addEventListener('keydown', function(event) {
        if ((event.keyCode === 461 || event.key === 'Escape') && pairs.some(function(pair) {
                return !pair.input.readOnly;
            })) {
            event.preventDefault();
            finishEditing();
        }
    });
}
setupFormEditors(['host', 'port', 'piccapPort', 'captureInterval', 'token', 'customProfile'], document, window);
