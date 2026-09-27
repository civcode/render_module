// Stable wire IDs, DOM physical codes, private RenderKey names. Never renumber.
// CMake generates the C++ mapping from this table, not from enum ordinals.
export const protocolKeys = [
[1, "KeyA", "A"], [2, "KeyB", "B"], [3, "KeyC", "C"], [4, "KeyD", "D"],
[5, "KeyE", "E"], [6, "KeyF", "F"], [7, "KeyG", "G"], [8, "KeyH", "H"],
[9, "KeyI", "I"], [10, "KeyJ", "J"], [11, "KeyK", "K"], [12, "KeyL", "L"],
[13, "KeyM", "M"], [14, "KeyN", "N"], [15, "KeyO", "O"], [16, "KeyP", "P"],
[17, "KeyQ", "Q"], [18, "KeyR", "R"], [19, "KeyS", "S"], [20, "KeyT", "T"],
[21, "KeyU", "U"], [22, "KeyV", "V"], [23, "KeyW", "W"], [24, "KeyX", "X"],
[25, "KeyY", "Y"], [26, "KeyZ", "Z"],
[27, "Digit0", "Digit0"], [28, "Digit1", "Digit1"], [29, "Digit2", "Digit2"],
[30, "Digit3", "Digit3"], [31, "Digit4", "Digit4"], [32, "Digit5", "Digit5"],
[33, "Digit6", "Digit6"], [34, "Digit7", "Digit7"], [35, "Digit8", "Digit8"], [36, "Digit9", "Digit9"],
[37, "Escape", "Escape"], [38, "Enter", "Enter"], [39, "Tab", "Tab"],
[40, "Backspace", "Backspace"], [41, "Delete", "Delete"], [42, "Insert", "Insert"], [43, "Space", "Space"],
[44, "ArrowLeft", "Left"], [45, "ArrowRight", "Right"], [46, "ArrowUp", "Up"], [47, "ArrowDown", "Down"],
[48, "Home", "Home"], [49, "End", "End"], [50, "PageUp", "PageUp"], [51, "PageDown", "PageDown"],
[52, "F1", "F1"], [53, "F2", "F2"], [54, "F3", "F3"], [55, "F4", "F4"],
[56, "F5", "F5"], [57, "F6", "F6"], [58, "F7", "F7"], [59, "F8", "F8"],
[60, "F9", "F9"], [61, "F10", "F10"], [62, "F11", "F11"], [63, "F12", "F12"],
[64, "ShiftLeft", "LeftShift"], [65, "ShiftRight", "RightShift"],
[66, "ControlLeft", "LeftCtrl"], [67, "ControlRight", "RightCtrl"],
[68, "AltLeft", "LeftAlt"], [69, "AltRight", "RightAlt"],
[70, "MetaLeft", "LeftSuper"], [71, "MetaRight", "RightSuper"],
[72, "Quote", "Apostrophe"], [73, "Comma", "Comma"], [74, "Minus", "Minus"], [75, "Period", "Period"],
[76, "Slash", "Slash"], [77, "Semicolon", "Semicolon"], [78, "Equal", "Equal"],
[79, "BracketLeft", "LeftBracket"], [80, "Backslash", "Backslash"], [81, "BracketRight", "RightBracket"],
[82, "Backquote", "GraveAccent"], [83, "CapsLock", "CapsLock"], [84, "ScrollLock", "ScrollLock"],
[85, "NumLock", "NumLock"], [86, "PrintScreen", "PrintScreen"], [87, "Pause", "Pause"], [88, "ContextMenu", "Menu"],
[89, "Numpad0", "Keypad0"], [90, "Numpad1", "Keypad1"], [91, "Numpad2", "Keypad2"],
[92, "Numpad3", "Keypad3"], [93, "Numpad4", "Keypad4"], [94, "Numpad5", "Keypad5"],
[95, "Numpad6", "Keypad6"], [96, "Numpad7", "Keypad7"], [97, "Numpad8", "Keypad8"], [98, "Numpad9", "Keypad9"],
[99, "NumpadDecimal", "KeypadDecimal"], [100, "NumpadDivide", "KeypadDivide"], [101, "NumpadMultiply", "KeypadMultiply"],
[102, "NumpadSubtract", "KeypadSubtract"], [103, "NumpadAdd", "KeypadAdd"],
[104, "NumpadEnter", "KeypadEnter"], [105, "NumpadEqual", "KeypadEqual"]
];
export const keyByCode = new Map(protocolKeys.map(([id, code, name]) => [code, {id, name}]));
export const keyByName = new Map(protocolKeys.map(([id, , name]) => [name, id]));
