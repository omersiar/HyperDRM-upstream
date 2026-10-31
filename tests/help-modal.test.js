'use strict';
const assert = require('assert'), fs = require('fs'), vm = require('vm');
/* Minimal stand-in for the app document: only the modal parts and one help
 * button per topic, which is all help.js touches. */
const handlers = {}, elements = {};
function el(id) {
    return {
        id, textContent: '', style: {}, handlers: {}, focused: 0,
        addEventListener(type, fn) { this.handlers[type] = fn; },
        focus() { this.focused++; doc.activeElement = this; },
        click() { if (this.onclick) this.onclick({ target: this }); }
    };
}
['helpModal', 'helpTitle', 'helpBody', 'helpClose'].forEach(id => { elements[id] = el(id); });
const helpKeys = ['automatic', 'calibration', 'receiver', 'output', 'network',
    'profile', 'geometry', 'compensate', 'colour'];
helpKeys.forEach(key => { elements['help-' + key] = el('help-' + key); });
const doc = {
    activeElement: null,
    getElementById(id) { return elements[id]; },
    addEventListener(type, fn) { (handlers[type] || (handlers[type] = [])).push(fn); }
};
const win = {};
const context = { document: doc, window: win, Object };
vm.createContext(context);
vm.runInContext(fs.readFileSync('app/help.js', 'utf8'), context);

/* Every topic opens, shows its own title and body, and takes the remote. */
helpKeys.forEach(key => {
    const before = elements.helpClose.focused;
    elements['help-' + key].click();
    assert.strictEqual(elements.helpModal.style.display, 'block', 'modal opens for ' + key);
    assert(elements.helpTitle.textContent.length > 3, 'title for ' + key);
    assert(elements.helpBody.textContent.length > 40, 'body for ' + key);
    assert.deepStrictEqual(Array.from(win.remoteNavigationScope), ['helpClose']);
    assert.strictEqual(elements.helpClose.focused, before + 1, 'close is focused for ' + key);
    elements.helpClose.click();
    assert.strictEqual(elements.helpModal.style.display, 'none');
    assert.strictEqual(win.remoteNavigationScope, null);
});
/* Distinct topics do not share text. */
elements['help-colour'].click();
const colourBody = elements.helpBody.textContent;
elements.helpClose.click();
elements['help-geometry'].click();
assert.notStrictEqual(elements.helpBody.textContent, colourBody);
assert(elements.helpTitle.textContent === 'Geometry');
elements.helpClose.click();
/* Back/Escape closes, and focus returns to the button that opened it. */
const openerFocus = elements['help-profile'].focused;
elements['help-profile'].click();
let prevented = false;
handlers.keydown.forEach(fn => fn({ keyCode: 461, preventDefault() { prevented = true; } }));
assert(prevented && elements.helpModal.style.display === 'none');
assert.strictEqual(elements['help-profile'].focused, openerFocus + 1, 'focus restored to the opener');
/* Escape outside the modal does nothing, and a backdrop click closes. */
handlers.keydown.forEach(fn => fn({ key: 'Escape', preventDefault() { throw new Error('should not prevent'); } }));
elements['help-automatic'].click();
elements.helpModal.onclick({ target: elements.helpModal });
assert.strictEqual(elements.helpModal.style.display, 'none');
elements['help-automatic'].click();
elements.helpModal.onclick({ target: elements.helpTitle });
assert.strictEqual(elements.helpModal.style.display, 'block', 'a click inside the card keeps it open');
elements.helpClose.click();
assert.strictEqual(elements.helpModal.style.display, 'none');
assert.strictEqual(win.remoteNavigationScope, null);
console.log('PASS: help modal opens per topic, scopes the remote, restores focus and closes by Back, backdrop or Close');
