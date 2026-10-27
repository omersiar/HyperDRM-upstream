/* Directional control for webOS remotes and standard keyboards. */
function setupRemoteNavigation(buttons, doc, win) {
    var lastFocused = buttons[0],
        enterDown = false;
    buttons.forEach(function(button) {
        button.addEventListener('focus', function() {
            lastFocused = button;
        });
        button.addEventListener('mouseenter', function() {
            if (!button.disabled) button.focus();
        });
    });
    doc.addEventListener('keydown', function(event) {
        var code = event.keyCode || event.which;
        var direction = event.key === 'ArrowLeft' || event.key === 'ArrowUp' || code === 37 || code === 38 ? -1 :
            event.key === 'ArrowRight' || event.key === 'ArrowDown' || code === 39 || code === 40 ? 1 : 0;
        var enter = event.key === 'Enter' || code === 13;
        if (!direction && !enter) return;
        var editing = doc.activeElement && !doc.activeElement.readOnly && (doc.activeElement.tagName === 'INPUT' && doc.activeElement.type !== 'checkbox' || doc.activeElement.tagName === 'TEXTAREA');
        if (editing) return;
        event.preventDefault();
        var enabled = buttons.filter(function(button) {
            return !button.disabled && (typeof button.getClientRects !== 'function' || button.getClientRects().length) && (!win.remoteNavigationScope || win.remoteNavigationScope.indexOf(button.id) >= 0);
        });
        if (!enabled.length) return;
        var focused = doc.activeElement,
            index = enabled.indexOf(focused);
        if (direction) {
            if (index < 0) {
                index = enabled.indexOf(lastFocused);
                if (index < 0) index = 0;
                enabled[index].focus();
            } else enabled[(index + direction + enabled.length) % enabled.length].focus();
        } else {
            if (enterDown || event.repeat) return;
            enterDown = true;
            if (index < 0) {
                index = enabled.indexOf(lastFocused);
                enabled[index < 0 ? 0 : index].focus();
            } else focused.click();
        }
    });
    doc.addEventListener('keyup', function(event) {
        if (event.key === 'Enter' || event.keyCode === 13 || event.which === 13) enterDown = false;
    });
    win.addEventListener('blur', function() {
        enterDown = false;
    });
}
