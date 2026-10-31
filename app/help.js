/* Plain-language help for every option.
 *
 * The remote cannot hover, and the paragraphs that used to sit under the
 * controls pushed the panels past the bottom of the screen, so each option gets
 * a ? button that opens one modal. The modal is the only place with a long
 * explanation, and it is written for someone watching TV, not for a developer.
 *
 * While the modal is open the remote scope is narrowed to its Close button, so
 * arrow keys and OK cannot reach controls hidden behind the overlay.
 */
var HELP = {
    automatic: {
        title: 'Automatic operation',
        body: 'Autostart starts HyperDRM when the TV switches on and keeps trying ' +
            'until your receiver answers, so you do not have to open this app on ' +
            'every boot. Auto PicCap is the recommended way to run both: turn ' +
            'PicCap\'s own autostart off and leave it to HyperDRM. HyperDRM then ' +
            'starts PicCap for normal video, because it captures a sharper ' +
            'picture, and switches to its own capture for protected playback, ' +
            'which PicCap can only record as black. Nothing switches while you ' +
            'browse a player\'s menus.'
    },
    calibration: {
        title: 'Auto Calibration',
        body: 'HyperDRM plays a two-minute test video full screen and learns where ' +
            'your TV draws each colour. Start it, leave the TV alone with no menus ' +
            'or overlays on screen, then save the result. Your current profile ' +
            'keeps working until you press Save profile, and you can always return ' +
            'to the built-in profile.'
    },
    receiver: {
        title: 'Which receiver?',
        body: 'The receiver is the app that drives your LEDs: Hyperion or ' +
            'HyperHDR. Pick the one you actually run. If the LEDs never react, ' +
            'check this setting first.'
    },
    output: {
        title: 'JSON or FlatBuffer?',
        body: 'JSON talks to a single LED setup at a time. FlatBuffer sends one ' +
            'picture that HyperHDR shares with every LED setup you have ' +
            'configured, and it needs the FlatBuffer server switched on in ' +
            'HyperHDR. Use JSON for Hyperion.'
    },
    network: {
        title: 'Address, ports and timing',
        body: 'These say where your receiver listens. Leave them alone unless you ' +
            'moved Hyperion or HyperHDR to another device or port: 19444 is the ' +
            'usual JSON port and 19400 the usual FlatBuffer port. Capture interval ' +
            'is how often HyperDRM reads the screen; 50 ms is a good balance, and ' +
            'lower values react faster but make the TV work harder. The API token ' +
            'is only needed if you protected the receiver with one.'
    },
    profile: {
        title: 'Which profile?',
        body: 'A profile remembers how your TV maps the screen to the sixteen ' +
            'readings. Auto picks the measurement for your TV model when one ' +
            'exists. Average is a neutral fallback for TVs that have not been ' +
            'measured. Profile JSON is for editing a saved profile by hand.'
    },
    geometry: {
        title: 'Geometry',
        body: 'This decides how the readings are painted for the receiver. ' +
            'Perimeter blends smoothly around the screen edge and always works. ' +
            'Region rectangles follow the areas your TV really samples, which ' +
            'matches the picture more closely, and falls back to the perimeter ' +
            'when nothing was measured. Reconstructed also uses those areas but ' +
            'solves them for one smooth picture instead of copying the nearest ' +
            'area, so gradients stay clean and the boundaries between areas stop ' +
            'showing. It softens real edges that fall on those boundaries, so try ' +
            'both and keep the one you prefer.'
    },
    grade: {
        title: 'Brightness and colour',
        body: 'A second pass over the finished colours, in a space where ' +
            'brightness and colour are separate. Gentle and Strong lift muted ' +
            'colours most and leave already-strong colours and whites alone, ' +
            'hold the strip below full panel brightness, and keep a small floor ' +
            'so a black scene is a glow rather than a hole. Off leaves the ' +
            'measured colours untouched.'
    },
    compensate: {
        title: 'Colour cast compensation',
        body: 'If your picture is set to Warm or Eye Comfort, the screen looks ' +
            'white while the LEDs look orange. This corrects that using the black ' +
            'and white readings from calibration. It is off by default.'
    },
    colour: {
        title: 'Colour',
        body: 'This decides how each area of the screen becomes one colour. Mean ' +
            'is the plain average and suits most viewing. Active ignores the black ' +
            'background, so a small bright object is not lost in a dark scene. ' +
            'p50, p90 and Peak keep more of the brightest parts, but Peak follows ' +
            'a single bright pixel and can look jumpy. Vivid averages the same ' +
            'pixels after spreading each channel away from mid-grey, so colours ' +
            'read stronger without inventing a hue a grey does not have.'
    }
};
var helpOpener = null;
function helpVisible() {
    var modal = document.getElementById('helpModal');
    return !!modal && modal.style.display === 'block';
}
function helpOpen(key, opener) {
    var entry = HELP[key];
    if (!entry) return;
    document.getElementById('helpTitle').textContent = entry.title;
    document.getElementById('helpBody').textContent = entry.body;
    document.getElementById('helpModal').style.display = 'block';
    helpOpener = opener || null;
    /* Keep the remote inside the modal while it is open. */
    window.remoteNavigationScope = ['helpClose'];
    var close = document.getElementById('helpClose');
    if (close) close.focus();
}
function helpClose() {
    if (!helpVisible()) return;
    document.getElementById('helpModal').style.display = 'none';
    window.remoteNavigationScope = null;
    var opener = helpOpener;
    helpOpener = null;
    if (opener && opener.focus) opener.focus();
}
Object.keys(HELP).forEach(function(key) {
    var button = document.getElementById('help-' + key);
    if (!button) return;
    button.onclick = function() {
        helpOpen(key, button);
    };
});
(function() {
    var close = document.getElementById('helpClose'),
        modal = document.getElementById('helpModal');
    if (close) close.onclick = helpClose;
    if (modal) modal.onclick = function(event) {
        if (event.target === modal) helpClose();
    };
    document.addEventListener('keydown', function(event) {
        if (!helpVisible()) return;
        if (event.key === 'Escape' || event.keyCode === 461) {
            event.preventDefault();
            helpClose();
        }
    });
})();
