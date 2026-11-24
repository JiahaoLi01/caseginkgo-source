// 键帽：按键的样子（键盘的键、鼠标、手柄的键）。设置界面的键位绑定、武器的界面（弹种轮盘底下的提示）共用，由 C++ 接进页面（Ginkgo::UI，见 GinkgoUiTheme.h）。
//   GinkgoKeycap.create(parent, key, label, theme, size)：在 parent 里画一个键，返回它的元素。
//     key：引擎的键名（FKey 的名字：R、LeftMouseButton、Gamepad_FaceButton_Left……）；label：引擎给的名字（表里没有的键写它）；
//     theme：'hud'（游戏画面上，白的键帽）或 'paper'（设置界面的纸）；size：键的高（px）。
//   GinkgoKeycap.NAMES：键名到中文名（设置界面的说明文字用）。
// 键盘的键：带厚度的键帽写着字。鼠标：一只鼠标，按的那个键涂实。手柄：面键是圆的（A B X Y 用手柄上的颜色），肩键、扳机各一种形状，十字键涂实一臂，摇杆是一个圈。
// weft 的 SVG 不认 viewBox：图都在 24 的格子里画，用 <g transform="scale()"> 缩放（同 paper.js 的图标）。
(() => {
  const NAMES = {
    SpaceBar: '空格', LeftControl: '左 Ctrl', RightControl: '右 Ctrl', LeftShift: '左 Shift', RightShift: '右 Shift', LeftAlt: '左 Alt', RightAlt: '右 Alt',
    Enter: '回车', Tab: 'Tab', CapsLock: '大写锁定', BackSpace: '退格', Insert: 'Insert', Home: 'Home', End: 'End', PageUp: 'PgUp', PageDown: 'PgDn',
    Up: '上', Down: '下', Left: '左', Right: '右',
    Zero: '0', One: '1', Two: '2', Three: '3', Four: '4', Five: '5', Six: '6', Seven: '7', Eight: '8', Nine: '9',
    LeftMouseButton: '鼠标左键', RightMouseButton: '鼠标右键', MiddleMouseButton: '鼠标中键', ThumbMouseButton: '侧键 1', ThumbMouseButton2: '侧键 2',
    MouseScrollUp: '滚轮上滚', MouseScrollDown: '滚轮下滚',
    Gamepad_FaceButton_Bottom: 'A', Gamepad_FaceButton_Right: 'B', Gamepad_FaceButton_Left: 'X', Gamepad_FaceButton_Top: 'Y',
    Gamepad_LeftShoulder: 'LB', Gamepad_RightShoulder: 'RB', Gamepad_LeftTrigger: 'LT', Gamepad_RightTrigger: 'RT',
    Gamepad_LeftThumbstick: '按左摇杆', Gamepad_RightThumbstick: '按右摇杆',
    Gamepad_DPad_Up: '十字上', Gamepad_DPad_Down: '十字下', Gamepad_DPad_Left: '十字左', Gamepad_DPad_Right: '十字右',
  };

  // 键盘上写在键帽上的字：比说明文字短（窄的键帽放得下）
  const CAP_TEXT = {
    SpaceBar: '空格', LeftControl: 'Ctrl', RightControl: 'Ctrl', LeftShift: 'Shift', RightShift: 'Shift', LeftAlt: 'Alt', RightAlt: 'Alt', Escape: 'Esc', CapsLock: 'Caps',
    BackSpace: '退格', PageUp: 'PgUp', PageDown: 'PgDn', Delete: 'Del', Insert: 'Ins',
  };
  // 左右之分写在角上
  const SIDE = {LeftControl: '左', RightControl: '右', LeftShift: '左', RightShift: '右', LeftAlt: '左', RightAlt: '右'};

  const PAD_FACE = {Gamepad_FaceButton_Bottom: ['A', 'gk-a'], Gamepad_FaceButton_Right: ['B', 'gk-b'], Gamepad_FaceButton_Left: ['X', 'gk-x'], Gamepad_FaceButton_Top: ['Y', 'gk-y']};
  const PAD_SHOULDER = {Gamepad_LeftShoulder: 'LB', Gamepad_RightShoulder: 'RB'};
  const PAD_TRIGGER = {Gamepad_LeftTrigger: 'LT', Gamepad_RightTrigger: 'RT'};
  const PAD_STICK = {Gamepad_LeftThumbstick: 'L', Gamepad_RightThumbstick: 'R'};
  const PAD_DPAD = {Gamepad_DPad_Up: 'up', Gamepad_DPad_Down: 'down', Gamepad_DPad_Left: 'left', Gamepad_DPad_Right: 'right'};
  const PAD_SPECIAL = {Gamepad_Special_Left: 'View', Gamepad_Special_Right: 'Menu'};
  const MOUSE = {LeftMouseButton: 'left', RightMouseButton: 'right', MiddleMouseButton: 'middle', MouseScrollUp: 'up', MouseScrollDown: 'down', ThumbMouseButton: 'back', ThumbMouseButton2: 'forward'};

  // 24 的格子里的路径
  const MOUSE_BODY = 'M12 2.5C16.6 2.5 19 5.4 19 9.5V14.5C19 18.9 16.2 21.5 12 21.5C7.8 21.5 5 18.9 5 14.5V9.5C5 5.4 7.4 2.5 12 2.5Z';
  const MOUSE_LEFT = 'M12 2.5C7.4 2.5 5 5.4 5 9.5V10.5H12Z';
  const MOUSE_RIGHT = 'M12 2.5C16.6 2.5 19 5.4 19 9.5V10.5H12Z';
  const MOUSE_LINES = 'M5 10.5H19M12 2.5V4.4M12 9.2V10.5';
  const MOUSE_WHEEL = 'M10.8 5.6a1.2 1.2 0 0 1 2.4 0v2.4a1.2 1.2 0 0 1-2.4 0Z';
  const THUMB_FORWARD = 'M2.8 10.6h2.2v3.2H2.8Z';
  const THUMB_BACK = 'M2.8 14.6h2.2v3.2H2.8Z';
  const SCROLL_UP = 'M21.4 4.4l2.2 3.4h-4.4Z';
  const SCROLL_DOWN = 'M21.4 11.4l-2.2-3.4h4.4Z';
  const DPAD = 'M9 2.5h6V9h6.5v6H15v6.5H9V15H2.5V9H9Z';
  const DPAD_ARM = {up: 'M9.8 3.3h4.4V9H9.8Z', down: 'M9.8 15h4.4v5.7H9.8Z', left: 'M3.3 9.8H9v4.4H3.3Z', right: 'M15 9.8h5.7v4.4H15Z'};

  const svgNs = 'http://www.w3.org/2000/svg';
  const svgEl = (parent, tag, attributes) => {
    const node = document.createElementNS(svgNs, tag);
    for (const name in attributes) node.setAttribute(name, attributes[name]);
    parent.appendChild(node);
    return node;
  };
  const span = (parent, className, text) => {
    const node = document.createElement('span');
    node.className = className;
    if (text !== undefined) node.textContent = text;
    parent.appendChild(node);
    return node;
  };
  const glyph = (node, size) => svgEl(svgEl(node, 'svg', {width: size, height: size}), 'g', {transform: `scale(${size / 24})`});
  const path = (group, d, cls) => svgEl(group, 'path', {d, class: cls});

  const drawMouse = (group, part) => {
    path(group, MOUSE_BODY, 'gk-body');
    if (part === 'left') path(group, MOUSE_LEFT, 'gk-hot');
    if (part === 'right') path(group, MOUSE_RIGHT, 'gk-hot');
    path(group, MOUSE_BODY, 'gk-line');
    path(group, MOUSE_LINES, 'gk-line');
    path(group, MOUSE_WHEEL, part === 'middle' || part === 'up' || part === 'down' ? 'gk-hot' : 'gk-body');
    path(group, MOUSE_WHEEL, 'gk-line');
    if (part === 'up') path(group, SCROLL_UP, 'gk-hot');
    if (part === 'down') path(group, SCROLL_DOWN, 'gk-hot');
    if (part === 'back' || part === 'forward') {
      path(group, THUMB_FORWARD, part === 'forward' ? 'gk-hot' : 'gk-body');
      path(group, THUMB_BACK, part === 'back' ? 'gk-hot' : 'gk-body');
      path(group, THUMB_FORWARD, 'gk-line');
      path(group, THUMB_BACK, 'gk-line');
    }
  };

  const drawDpad = (group, arm) => {
    path(group, DPAD, 'gk-body');
    path(group, DPAD_ARM[arm], 'gk-hot');
    path(group, DPAD, 'gk-line');
  };

  /** 在 parent 里画一个键，返回它的元素（说明见文件开头） */
  const create = (parent, key, label, theme = 'paper', size = 24) => {
    const themed = `gk gk-${theme}`;
    const fontSize = Math.round(size * 0.54);
    const box = (node, width) => {
      node.style.height = `${size}px`;
      node.style.fontSize = `${fontSize}px`;
      if (width) node.style.width = `${width}px`;
      return node;
    };
    if (MOUSE[key]) {
      const node = box(span(parent, `${themed} gk-glyph`), size + (MOUSE[key] === 'up' || MOUSE[key] === 'down' ? 0 : 0));
      drawMouse(glyph(node, size), MOUSE[key]);
      return node;
    }
    if (PAD_DPAD[key]) {
      const node = box(span(parent, `${themed} gk-glyph`), size);
      drawDpad(glyph(node, size), PAD_DPAD[key]);
      return node;
    }
    if (PAD_FACE[key]) return box(span(parent, `${themed} gk-face ${PAD_FACE[key][1]}`, PAD_FACE[key][0]), size);
    if (PAD_SHOULDER[key]) return box(span(parent, `${themed} gk-shoulder`, PAD_SHOULDER[key]));
    if (PAD_TRIGGER[key]) return box(span(parent, `${themed} gk-trigger`, PAD_TRIGGER[key]));
    if (PAD_STICK[key]) return box(span(parent, `${themed} gk-stick`, PAD_STICK[key]), size);
    if (PAD_SPECIAL[key]) return box(span(parent, `${themed} gk-special`, PAD_SPECIAL[key]));
    // 键盘（认不出的键也是一个写着名字的键帽）
    const node = box(span(parent, `${themed} gk-kb`));
    node.style.minWidth = `${size}px`;
    node.style.paddingLeft = node.style.paddingRight = `${Math.round(size * 0.28)}px`;
    if (SIDE[key]) {
      const side = span(node, 'gk-side', SIDE[key]);
      side.style.fontSize = `${Math.max(Math.round(size * 0.4), 9)}px`;
    }
    span(node, 'gk-text', CAP_TEXT[key] || NAMES[key] || label || key);
    return node;
  };

  globalThis.GinkgoKeycap = {NAMES, create};
})();
