// Phase 5 protocol v1. No browser/DOM vocabulary crosses into RemoteInputBackend.
export function contentRect(cw, ch, fw, fh) {
    if (![cw, ch, fw, fh].every(n => Number.isFinite(n) && n > 0)) return null;
    const scale = Math.min(cw / fw, ch / fh), width = fw * scale, height = fh * scale;
    return { left: (cw - width) / 2, top: (ch - height) / 2, width, height };
}
export function normalizedPoint(x, y, rect, captured = false) {
    if (!rect || !Number.isFinite(x) || !Number.isFinite(y)) return null;
    const nx = (x - rect.left) / rect.width, ny = (y - rect.top) / rect.height;
    if (!captured && (nx < 0 || nx > 1 || ny < 0 || ny > 1)) return null;
    return { x: Math.max(0, Math.min(1, nx)), y: Math.max(0, Math.min(1, ny)) };
}
export function normalizeWheel(x, y, mode) {
    // 100 pixels, 3 lines, or 0.1 pages = one logical wheel unit. ImGui sign is opposite DOM.
    const unit = mode === 0 ? 100 : mode === 1 ? 3 : .1;
    const convert = n => Number.isFinite(n) ? Math.max(-1000, Math.min(1000, -n / unit)) : 0;
    return { horizontal: convert(x), vertical: convert(y) };
}
export function protocolKey(code) {
    if (/^Key[A-Z]$/.test(code)) return code.slice(3);
    if (/^Digit[0-9]$/.test(code) || /^F([1-9]|1[0-2])$/.test(code)) return code;
    if (/^Numpad[0-9]$/.test(code)) return 'Keypad' + code.slice(6);
    const special = {
        ShiftLeft: 'LeftShift', ShiftRight: 'RightShift', ControlLeft: 'LeftCtrl', ControlRight: 'RightCtrl',
        AltLeft: 'LeftAlt', AltRight: 'RightAlt', MetaLeft: 'LeftSuper', MetaRight: 'RightSuper',
        ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down', Quote: 'Apostrophe',
        BracketLeft: 'LeftBracket', BracketRight: 'RightBracket', Backquote: 'GraveAccent', ContextMenu: 'Menu',
        NumpadDecimal: 'KeypadDecimal', NumpadDivide: 'KeypadDivide', NumpadMultiply: 'KeypadMultiply',
        NumpadSubtract: 'KeypadSubtract', NumpadAdd: 'KeypadAdd', NumpadEnter: 'KeypadEnter', NumpadEqual: 'KeypadEqual'
    };
    const same = new Set(['Escape', 'Enter', 'Tab', 'Backspace', 'Delete', 'Insert', 'Space', 'Home', 'End',
        'PageUp', 'PageDown', 'Comma', 'Minus', 'Period', 'Slash', 'Semicolon', 'Equal', 'Backslash',
        'CapsLock', 'ScrollLock', 'NumLock', 'PrintScreen', 'Pause']);
    return special[code] || (same.has(code) ? code : null);
}
export function parseFrame(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength < 28) throw Error('Truncated frame');
    const h = new DataView(data), id = h.getBigUint64(8), width = h.getUint32(16), height = h.getUint32(20), size = h.getUint32(24);
    if (h.getUint32(0) !== 0x524d4a50 || h.getUint16(4) !== 1 || h.getUint16(6) !== 1 ||
        id < 1n || !width || !height || width > 2048 || height > 2048 ||
        !size || size > 16 * 1024 * 1024 || data.byteLength !== size + 28) throw Error('Invalid frame');
    return { id: id.toString(), width, height, jpeg: data.slice(28) };
}
export function utf8Chunks(text, maxBytes = 256) {
    const encoder = new TextEncoder(), out = []; let part = '', bytes = 0;
    for (const character of text) {
        const size = encoder.encode(character).length;
        if (bytes + size > maxBytes) { out.push(part); part = ''; bytes = 0; }
        part += character; bytes += size;
    }
    if (part) out.push(part);
    return out;
}

function start() {
    const display = document.querySelector('#display'), canvas = document.querySelector('#picture');
    const surface = document.querySelector('#input'), text = document.querySelector('#text');
    const connection = document.querySelector('#connection'), lease = document.querySelector('#lease');
    const diagnostics = document.querySelector('#diagnostics');
    const video = document.querySelector('#video'), play = document.querySelector('#play');
    let transport = 'jpeg', pc = null, signalChain = Promise.resolve(), remoteIce = [];
    function closePeer() {
        if (pc) { pc.ontrack = pc.onicecandidate = pc.onconnectionstatechange = null; pc.close(); pc = null; }
        remoteIce = []; video.srcObject = null; video.dataset.framesDecoded = '0'; play.hidden = true;
    }
    function signaling(type, data = {}) { return send(type, {session: connection.dataset.session, ...data}); }
    async function mediaSignal(message, current) {
        if (socket !== current || current.readyState !== WebSocket.OPEN) return;
        if (message.session !== connection.dataset.session) throw Error('stale media session');
        if (message.type === 'hello') {
            if (pc || !Array.isArray(message.iceServers) || message.iceServers.length > 8) throw Error('invalid hello');
            const peer = new RTCPeerConnection({iceServers: message.iceServers, iceTransportPolicy: message.iceTransportPolicy});
            pc = peer; remoteIce = []; video.dataset.offers = '0';
            let iceEnded = false;
            peer.onicecandidate = event => {
                if (pc !== peer || socket !== current || iceEnded) return;
                if (event.candidate?.candidate) signaling('ice-candidate', {candidate: event.candidate.candidate, mid: event.candidate.sdpMid});
                else { iceEnded = true; signaling('ice-complete'); }
            };
            peer.ontrack = event => {
                if (pc !== peer || event.track.kind !== 'video') return;
                video.srcObject = event.streams[0] || new MediaStream([event.track]);
                video.play().catch(() => { if (pc === peer) play.hidden = false; });
            };
            peer.onconnectionstatechange = () => {
                if (pc !== peer) return;
                video.dataset.connection = peer.connectionState;
                if (peer.connectionState === 'failed') current.close();
            };
        } else if (message.type === 'offer') {
            const peer = pc;
            if (!peer || typeof message.sdp !== 'string' || message.sdp.length > 32768 || peer.remoteDescription) throw Error('invalid offer');
            await peer.setRemoteDescription({type: 'offer', sdp: message.sdp});
            if (pc !== peer || socket !== current) return;
            for (const candidate of remoteIce) await peer.addIceCandidate(candidate);
            remoteIce = [];
            const answer = await peer.createAnswer();
            const h264 = /^a=fmtp:96 (.*)$/m.exec(answer.sdp);
            const profile = h264 && /(?:^|;)profile-level-id=([0-9a-f]{6})(?:;|\r?$)/i.exec(h264[1]);
            if (!profile) throw Error('H264 answer missing profile');
            let receiveExtension = false;
            if (parseInt(profile[1].slice(4), 16) < 40) {
                // libwebrtc's software codec list defaults to level 3.1 even
                // when its decoder supports 1080p. Advertise a HIGHER receive
                // level only after querying that actual browser configuration.
                // RFC 6184 max-recv-level is profile-iop + level_idc (four hex digits).
                const capability = await navigator.mediaCapabilities?.decodingInfo({type: 'webrtc', video: {
                    contentType: 'video/H264;packetization-mode=1;profile-level-id=42e028',
                    width: 1920, height: 1080, bitrate: 12000000, framerate: 30
                }});
                if (!capability?.supported) throw Error('1080p H264 reception unsupported');
                receiveExtension = true;
            }
            await peer.setLocalDescription(answer);
            if (pc !== peer || socket !== current) return;
            video.dataset.offers = String(Number(video.dataset.offers) + 1);
            // Firefox drops unknown fmtp parameters when serializing local SDP.
            // Preserve this capability-checked RFC 6184 receive declaration on
            // the signaling wire; codec/profile and ICE/DTLS fields stay native.
            const sdp = receiveExtension ? peer.localDescription.sdp.replace(/^a=fmtp:96 ([^\r\n]*)/m,
                line => line.replace(/;max-recv-level=[^;\r\n]*/i, '') + ';max-recv-level=e028') : peer.localDescription.sdp;
            signaling('answer', {sdp});
        } else if (message.type === 'ice-candidate' || message.type === 'ice-complete') {
            if (!pc) throw Error('ICE without peer');
            const candidate = message.type === 'ice-complete' ? null : {candidate: message.candidate, sdpMid: message.mid};
            if (candidate && (typeof candidate.candidate !== 'string' || candidate.candidate.length > 1024 || candidate.sdpMid !== 'video')) throw Error('invalid ICE');
            if (pc.remoteDescription) await pc.addIceCandidate(candidate);
            else { if (remoteIce.length >= 65) throw Error('ICE overflow'); remoteIce.push(candidate); }
        } else if (message.type === 'webrtc-state') video.dataset.iceState = message.state;
        else if (message.type === 'error') { diagnostics.textContent = 'WebRTC failed; reconnecting. JPEG mode is available in server configuration.'; current.close(); }
    }
    video.addEventListener('loadedmetadata', layout);
    video.addEventListener('resize', layout);
    play.addEventListener('click', () => video.play().then(() => { play.hidden = true; }).catch(() => {}));
    setInterval(async () => {
        const peer = pc; if (!peer || peer.connectionState !== 'connected') return;
        try {
            const reports = await peer.getStats(); if (peer !== pc) return;
            const stats = {};
            for (const report of reports.values()) {
                if (report.type === 'inbound-rtp' && (report.kind === 'video' || report.mediaType === 'video'))
                    for (const field of ['framesReceived','framesDecoded','framesDropped','framesPerSecond','bytesReceived','packetsReceived','packetsLost','jitter','jitterBufferDelay'])
                        if (Number.isFinite(report[field])) stats[field] = report[field];
                if (report.type === 'candidate-pair' && report.state === 'succeeded' && report.nominated) {
                    stats.roundTripTime = report.currentRoundTripTime;
                    stats.localCandidateType = reports.get(report.localCandidateId)?.candidateType;
                    stats.remoteCandidateType = reports.get(report.remoteCandidateId)?.candidateType;
                }
            }
            if (stats.framesDecoded > 0) attempt = 0;
            video.dataset.stats = JSON.stringify(stats); video.dataset.framesDecoded = String(stats.framesDecoded || 0);
            diagnostics.textContent = `${video.videoWidth}×${video.videoHeight} · ${stats.framesDecoded || 0} decoded · ${stats.framesPerSecond || 0} fps · ${stats.localCandidateType || '?'}/${stats.remoteCandidateType || '?'}`;
        } catch { /* Closed peer; no adaptation or telemetry queue. */ }
    }, 1000);
    let socket, sequence = 0, controller = false, focused = false, pointer = null, composing = false;
    let frameWidth = 640, frameHeight = 480, lastId = 0n, rect, position = { x: .5, y: .5 };
    let move = null, retry, attempt = 0, resizeTimer, pending = null, decoding = false;
    let mods = { ctrl: false, shift: false, alt: false, super: false };
    const keys = new Set(), buttons = new Set();
    const bits = [['left', 1], ['right', 2], ['middle', 4], ['extra1', 8], ['extra2', 16]];
    function send(type, data = {}) {
        if (!socket || socket.readyState !== WebSocket.OPEN) return false;
        if (socket.bufferedAmount > 65536) { socket.close(); return false; }
        socket.send(JSON.stringify({ v: 1, type, seq: ++sequence, ...data })); return true;
    }
    function layout() {
        if (transport === 'webrtc' && video.videoWidth && video.videoHeight) { frameWidth = video.videoWidth; frameHeight = video.videoHeight; }
        rect = contentRect(display.clientWidth, display.clientHeight, frameWidth, frameHeight);
        if (!rect) return;
        for (const element of [canvas, video, surface]) Object.assign(element.style,
            { left: rect.left + 'px', top: rect.top + 'px', width: rect.width + 'px', height: rect.height + 'px' });
    }
    function viewport() {
        if (!controller) return;
        send('viewport', { width: Math.max(1, Math.round(display.clientWidth)), height: Math.max(1, Math.round(display.clientHeight)),
            devicePixelRatio: Math.max(.25, Math.min(8, window.devicePixelRatio || 1)) });
    }
    function snapshot() {
        if (controller) send('snapshot', { ...position, keys: [...keys], buttons: [...buttons], mods, focused });
    }
    function release(notify = true) {
        move = null; keys.clear(); buttons.clear(); focused = false; composing = false; text.value = '';
        mods = { ctrl: false, shift: false, alt: false, super: false };
        const old = pointer; pointer = null;
        if (old !== null && surface.hasPointerCapture(old)) surface.releasePointerCapture(old);
        if (notify && controller) { send('focus', { focused: false }); send('release_all'); }
    }
    function gain() {
        if (!controller) return;
        if (!focused) { focused = true; send('focus', { focused: true }); }
        text.focus({ preventScroll: true });
    }
    function flushMove() { if (move && controller) { send('mouse_move', move); move = null; } }
    function point(event, captured) {
        const bounds = display.getBoundingClientRect();
        return normalizedPoint(event.clientX - bounds.left, event.clientY - bounds.top, rect, captured);
    }
    function setMove(event, p) {
        position = p; move = { ...p, source: ['mouse', 'touch', 'pen'].includes(event.pointerType) ? event.pointerType : 'mouse' };
    }
    function syncButtons(mask) {
        flushMove();
        for (const [button, bit] of bits) {
            const down = !!(mask & bit);
            if (buttons.has(button) === down) continue;
            if (down) buttons.add(button); else buttons.delete(button);
            send('mouse_button', { button, down });
        }
    }
    surface.addEventListener('pointerdown', event => {
        if (!controller || (pointer !== null && pointer !== event.pointerId)) return;
        const p = point(event, false); if (!p) return;
        event.preventDefault(); gain(); pointer = event.pointerId; surface.setPointerCapture(pointer);
        setMove(event, p); syncButtons(event.buttons);
    });
    surface.addEventListener('pointermove', event => {
        if (!controller || (pointer !== null && pointer !== event.pointerId)) return;
        const p = point(event, pointer === event.pointerId); if (!p) return;
        setMove(event, p);
        if (pointer !== null) syncButtons(event.buttons); // Includes mouse-button chords.
    });
    surface.addEventListener('pointerup', event => {
        if (!controller || pointer !== event.pointerId) return;
        const p = point(event, true); if (p) setMove(event, p);
        syncButtons(event.buttons);
        if (!event.buttons) { const old = pointer; pointer = null; if (surface.hasPointerCapture(old)) surface.releasePointerCapture(old); }
    });
    surface.addEventListener('pointercancel', () => release());
    surface.addEventListener('lostpointercapture', () => { if (pointer !== null) release(); });
    surface.addEventListener('contextmenu', event => { if (controller) event.preventDefault(); });
    surface.addEventListener('wheel', event => {
        const p = point(event, false); if (!controller || !p) return;
        event.preventDefault(); gain(); setMove(event, p); flushMove();
        send('wheel', normalizeWheel(event.deltaX, event.deltaY, event.deltaMode));
    }, { passive: false });
    function keyboard(event, down) {
        if (!controller || !focused) return;
        const key = protocolKey(event.code);
        mods = { ctrl: event.ctrlKey, shift: event.shiftKey, alt: event.altKey, super: event.metaKey };
        if (key) {
            if (down) keys.add(key); else keys.delete(key);
            send('key', { key, down, repeat: event.repeat, mods });
        }
        // Let legitimate text/IME and paste reach the textarea; prevent remote
        // navigation keys and unrelated browser shortcuts from stealing focus.
        const clipboard = (event.ctrlKey || event.metaKey) && ['KeyV', 'KeyC', 'KeyX'].includes(event.code);
        const altGraph = event.getModifierState('AltGraph') || (event.ctrlKey && event.altKey && !event.metaKey);
        const printable = (!event.ctrlKey || altGraph) && !event.metaKey && (event.key.length === 1 || event.isComposing);
        if (!clipboard && !printable) event.preventDefault();
    }
    for (const element of [surface, text]) {
        element.addEventListener('keydown', e => keyboard(e, true));
        element.addEventListener('keyup', e => keyboard(e, false));
        element.addEventListener('blur', () => queueMicrotask(() => {
            if (document.activeElement !== surface && document.activeElement !== text) release();
        }));
    }
    surface.addEventListener('focus', gain);
    function commit() {
        if (composing || !focused || !controller) return;
        const value = text.value; text.value = '';
        if (value.length > 16384 || new TextEncoder().encode(value).length > 16384) {
            diagnostics.textContent = 'Text commit exceeds 16 KiB limit'; release(); return;
        }
        for (const chunk of utf8Chunks(value)) send('text', { text: chunk });
    }
    text.addEventListener('beforeinput', event => { if (!controller) event.preventDefault(); });
    text.addEventListener('compositionstart', () => { composing = true; });
    text.addEventListener('compositionupdate', () => {}); // Intermediate composition is not committed input.
    text.addEventListener('compositionend', () => { composing = false; setTimeout(commit, 0); });
    text.addEventListener('input', event => { if (!event.isComposing) commit(); });
    window.addEventListener('blur', () => release());
    window.addEventListener('focus', () => {
        if (document.activeElement === text || document.activeElement === surface) gain();
    });
    document.addEventListener('visibilitychange', () => { if (document.hidden) release(); });
    function setController(value) {
        if (!value && controller) release(false);
        controller = value; lease.textContent = value ? 'Controller' : 'View only'; lease.dataset.control = String(value);
        if (value) { viewport(); snapshot(); }
    }
    async function decode() {
        if (decoding) return;
        decoding = true;
        try {
            while (pending) {
                const item = pending; pending = null;
                const image = await createImageBitmap(new Blob([item.frame.jpeg], { type: 'image/jpeg' }));
                try {
                    if (socket !== item.socket || item.socket.readyState !== WebSocket.OPEN) continue;
                    if (image.width !== item.frame.width || image.height !== item.frame.height) throw Error('JPEG dimensions mismatch');
                    if (!pending) {
                        frameWidth = image.width; frameHeight = image.height; canvas.width = frameWidth; canvas.height = frameHeight;
                        layout(); canvas.getContext('2d').drawImage(image, 0, 0);
                        canvas.dataset.frameId = String(item.frame.id); canvas.dataset.width = String(frameWidth); canvas.dataset.height = String(frameHeight);
                        diagnostics.textContent = `${frameWidth}×${frameHeight} · frame ${item.frame.id}`;
                    }
                    send('frame_ack', { frameId: item.frame.id });
                } finally { image.close(); }
            }
        } catch { if (socket) socket.close(1002, 'Invalid frame'); }
        finally { decoding = false; }
    }
    function connect() {
        clearTimeout(retry); closePeer(); signalChain = Promise.resolve();
        release(false); setController(false); pending = null; sequence = 0; lastId = 0n;
        const url = new URL('/api/ws', location.href); url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const current = new WebSocket(url); socket = current; current.binaryType = 'arraybuffer';
        let pendingSignals = 0;
        connection.textContent = 'Connecting (sign in if authentication is enabled)';
        current.onopen = () => { if (socket === current) connection.textContent = 'Connected'; };
        current.onmessage = event => {
            if (socket !== current) return;
            try {
                if (typeof event.data === 'string') {
                    if (event.data.length > 65536) throw Error('message size');
                    const message = JSON.parse(event.data); if (message.v !== 1) throw Error('version');
                    if (message.type === 'welcome') {
                        connection.dataset.session = message.session; transport = message.transport || 'jpeg';
                        if (!['jpeg', 'webrtc'].includes(transport)) throw Error('transport');
                        if (transport === 'jpeg') attempt = 0;
                        canvas.hidden = transport === 'webrtc'; video.hidden = transport !== 'webrtc';
                        document.querySelector('#transport').textContent = transport === 'webrtc' ? 'WebRTC H.264' : 'DIAGNOSTIC JPEG/WebSocket';
                        setController(message.control);
                        if (transport === 'webrtc') signaling('hello');
                    }
                    if (['hello','offer','ice-candidate','ice-complete','webrtc-state','error'].includes(message.type)) {
                        if (transport !== 'webrtc' || ++pendingSignals > 96) throw Error('unexpected signaling');
                        signalChain = signalChain.then(() => mediaSignal(message, current)).catch(() => {
                            if (socket === current) { diagnostics.textContent = 'WebRTC negotiation failed; JPEG mode is available in server configuration.'; current.close(); }
                        }).finally(() => { --pendingSignals; });
                    }
                    if (message.type === 'lease') setController(message.control);
                    if (message.type === 'view_only') setController(false);
                    if (message.type === 'input_reset') release();
                    if (message.type === 'viewport_accepted') {
                        display.dataset.width = String(message.width); display.dataset.height = String(message.height);
                    }
                } else {
                    if (transport === 'webrtc') throw Error('JPEG in WebRTC mode');
                    const frame = parseFrame(event.data); if (BigInt(frame.id) <= lastId) throw Error('stale frame'); lastId = BigInt(frame.id);
                    pending = { frame, socket: current }; decode(); // One active decode, one replaceable pending frame.
                }
            } catch { current.close(1002, 'Protocol error'); }
        };
        current.onclose = () => {
            if (socket !== current) return;
            closePeer(); release(false); setController(false); pending = null; connection.textContent = 'Disconnected · retrying';
            retry = setTimeout(connect, Math.min(8000, 250 * 2 ** Math.min(attempt++, 5)));
        };
    }
    document.querySelector('#login').addEventListener('submit', async event => {
        event.preventDefault(); const field = document.querySelector('#token'), token = field.value; field.value = '';
        try {
            const response = await fetch('/api/login', { method: 'POST', headers: { Authorization: `Bearer ${token}` } });
            if (!response.ok) { connection.textContent = 'Authentication failed'; return; }
            if (socket) socket.close(); connect();
        } catch { connection.textContent = 'Connection failed'; }
    });
    document.querySelector('#reconnect').addEventListener('click', () => { if (socket) socket.close(); else connect(); });
    new ResizeObserver(() => { layout(); clearTimeout(resizeTimer); resizeTimer = setTimeout(viewport, 150); }).observe(display);
    setInterval(snapshot, 750);
    function animation() { flushMove(); requestAnimationFrame(animation); }
    requestAnimationFrame(animation); layout(); connect();
}
if (typeof document !== 'undefined') start();
