// 交互的提示（Docs/Design/Interaction.md「提示的界面」）：本地玩家附近能交互的目标，每个一个元素，跟着锚点走。
//
// 游戏在状态或文字变了时发一条事件 "interaction"（C++ 的 FGinkgoInteractionPrompt），值是
//   {key: {key, label}, items: [{id, state, text, reason, pending, flash, leaving}]}
//   key：交互键，最近一次输入的设备上绑的（引擎的键名与引擎给的名字；没绑时是空的），换设备、改键时跟着变；id：这一项的编号，也是锚点的 key；
//   state：Hint（空心圈）、Ready（焦点可以交互：圈里写键，旁边墨牌写 text）、Blocked（焦点不能交互：淡的空心圈，墨牌写 reason）；
//   pending：在等服务器回复（键换成转动的弧）；flash：刚被拒绝的原因，留 1 s；
//   leaving：这一项是刚不见了的目标（拾起、走远、被挡住）的残影，放消失的动画。游戏给残影另一个编号（原来的加上 2^40），
//   原来那一项同时不再出现；残影留 0.4 s，之后也不再出现，元素才拿掉。
// 桌前另带 bench。parts 按武器定义的顺序列出可用配件，名字、简介、图名和 effects（text、tone）都来自配件资产。
// 点击回传绘制时的武器槽和配件下标。键鼠和手柄快捷键共用页面选择，十字键左右选插槽、上下选配件。
// 每帧的位置走锚点（data-anchor），游戏不每帧发 JSON。没有框架，直接写 DOM。
//
// 出现与消失的动画都是 CSS（page.css），都在元素新建时放：出现的元素一建出来放 ix-enter、ix-ripple，残影一建出来放
// ix-exit、ix-burst。残影是另一个元素：目标在消失途中又出现时，新建的元素重放出现的动画，残影照样把消失放完，互不打断。
(() => {
  const engine = weft.engine;
  const root = document.getElementById('root');
  const items = new Map();  // id → {element, key, plate}

  const add = (parent, tag, cls) => {
    const e = document.createElement(tag);
    e.className = cls;
    parent.appendChild(e);
    return e;
  };

  const create = id => {
    const element = add(root, 'div', 'ix');
    element.setAttribute('data-anchor', id);
    add(element, 'div', 'ix-ripple');
    const body = add(element, 'div', 'ix-body');
    add(body, 'div', 'ix-ring');
    const key = add(body, 'div', 'ix-key');
    add(body, 'div', 'ix-spin');
    const label = add(body, 'div', 'ix-label');
    const plate = add(label, 'span', 'ix-plate');
    return {element, key, plate, keyId: null, keyClass: ''};
  };

  // 圈里的键（你说的：拾取的提示按输入设备换键位）。返回加在这一项上的类：
  // - 手柄的面键（A B X Y）：徽章涂成手柄上那个键的颜色、写白字（颜色同键帽，Theme/keycap.css）；
  // - 一个字的键（E、F、1）：照原来写墨色的字；
  // - 别的（鼠标的键、十字键、肩键、扳机、名字长的键）：缩小的键帽（Theme/keycap.js，C++ 载入页面时接进来）；
  // - 没绑键：写一个问号。
  const FACE = {Gamepad_FaceButton_Bottom: ['A', 'a'], Gamepad_FaceButton_Right: ['B', 'b'], Gamepad_FaceButton_Left: ['X', 'x'], Gamepad_FaceButton_Top: ['Y', 'y']};
  const CAP_SIZE = 18;
  const drawKey = (node, key) => {
    while (node.firstChild) node.removeChild(node.firstChild);
    if (!key || !key.key) {
      node.textContent = '?';
      return '';
    }
    if (FACE[key.key]) {
      node.textContent = FACE[key.key][0];
      return ' ix-face ix-face-' + FACE[key.key][1];
    }
    const text = GinkgoKeycap.NAMES[key.key] || key.label || key.key;
    if (text.length === 1) {
      node.textContent = text;
      return '';
    }
    GinkgoKeycap.create(node, key.key, key.label, 'hud', CAP_SIZE);
    return ' ix-cap';
  };

  const panel = add(root, 'div', 'bench-panel');
  const band = add(panel, 'div', 'bench-band');
  const header = add(band, 'div', 'bench-header');
  let currentBench = null;
  const select = slot => {
    if (currentBench && currentBench.ready && slot >= 0 && slot !== currentBench.selected)
      engine.send('benchSelect', slot);
  };
  const nav = (direction, label) => {
    const button = add(header, 'button', 'bench-nav');
    button.setAttribute('aria-label', label);
    button.addEventListener('click', () => { if (currentBench) select(currentBench[direction]); });
    return button;
  };
  const previous = nav('previous', '上一把武器');
  const title = add(header, 'div', 'bench-title');
  const next = nav('next', '下一把武器');
  const index = add(band, 'div', 'bench-index');
  const slots = add(index, 'div', 'bench-slots');
  const status = add(index, 'span', 'bench-status');
  const mountTabs = add(panel, 'div', 'bench-mounts');
  const mountNames = {handguard: '前端', stock: '枪托', grip: '主握把', magazine: '弹匣', trigger: '扳机', muzzle: '枪口', foregrip: '前握把'};
  const mountKey = part => String((part && part.mount) || 'muzzle').toLowerCase();
  let selectedMount = '';
  let selectedPart = -1;
  let lastBenchKey = {};
  let mountsKey = '';
  let lastFocusedKey = '';
  const notifyFocus = (slot, index) => {
    const key = slot >= 0 && index >= 0 ? slot + ':' + index : '';
    if (key === lastFocusedKey) return;
    lastFocusedKey = key;
    engine.send('benchFocus', {slot: key ? slot : -1, index: key ? index : -1});
  };
  const parts = add(panel, 'div', 'bench-parts');
  const partCards = [];
  const chooseMount = mount => {
    if (!currentBench || selectedMount === mount) return;
    selectedMount = mount;
    selectedPart = -1;
    engine.send('sound', 'Select');
    updateBench(currentBench, lastBenchKey);
  };
  const choosePart = index => {
    if (!currentBench || selectedPart === index) return;
    selectedPart = index;
    // 进桌放枪时，光标可能已经落在卡片上；此时只更新高亮。
    if (currentBench.ready) engine.send('sound', 'Focus');
    updateBench(currentBench, lastBenchKey);
  };
  const install = part => {
    if (!currentBench || !part) return;
    if (part.available) engine.send('benchInstall', {slot: currentBench.selected, index: part.index});
    else engine.send('sound', 'Invalid');
  };
  const createCard = () => {
    const element = add(parts, 'div', 'bench-part');
    const heading = add(element, 'div', 'bench-part-heading');
    const icon = add(heading, 'span', 'bench-part-icon');
    icon.setAttribute('aria-hidden', 'true');
    const name = add(heading, 'span', 'bench-part-name');
    const description = add(element, 'div', 'bench-part-description');
    const effects = add(element, 'div', 'bench-part-effects');
    effects.setAttribute('tabindex', '0');
    effects.setAttribute('aria-label', '配件效果');
    const button = add(element, 'button', 'bench-install');
    const key = add(button, 'span', 'bench-install-key');
    const label = add(button, 'span', '');
    const card = {element, button, icon, name, description, effects, effectRows: [], key, label, part: null, cap: ''};
    button.addEventListener('click', () => install(card.part));
    element.addEventListener('pointerenter', () => {
      if (card.part) choosePart(card.part.index);
    });
    return card;
  };
  const leave = add(panel, 'button', 'bench-leave');
  const exitKey = add(leave, 'span', 'bench-exit-key');
  const exitText = add(leave, 'span', '');
  leave.addEventListener('click', () => engine.send('benchLeave'));
  let slotOrder = '';
  let caps = '';
  const setCap = (node, key, label) => {
    while (node.firstChild) node.removeChild(node.firstChild);
    GinkgoKeycap.create(node, key, label, 'hud', 20);
  };

  const updateBench = (bench, key) => {
    if (!bench || !currentBench || bench.selected !== currentBench.selected) { selectedMount = ''; selectedPart = -1; }
    currentBench = bench;
    lastBenchKey = key;
    panel.style.display = bench ? 'flex' : 'none';
    if (!bench) { notifyFocus(-1, -1); return; }
    const selected = bench.weapons.find(weapon => weapon.slot === bench.selected);
    title.textContent = selected ? selected.name : '选择武器';
    const allParts = bench.parts || [];
    const mounts = [...new Set(allParts.map(mountKey))];
    if (!mounts.includes(selectedMount)) selectedMount = mounts[0] || '';
    const nextMountsKey = mounts.join(',') + ':' + selectedMount;
    mountTabs.style.display = mounts.length > 1 ? 'flex' : 'none';
    if (nextMountsKey !== mountsKey) {
      mountsKey = nextMountsKey;
      mountTabs.textContent = '';
      for (const mount of mounts) {
        const tab = add(mountTabs, 'button', 'bench-mount' + (mount === selectedMount ? ' bench-mount-current' : ''));
        tab.textContent = mountNames[mount] || mount;
        tab.addEventListener('click', () => chooseMount(mount));
      }
    }
    const choices = allParts.filter(part => mountKey(part) === selectedMount);
    if (!choices.some(part => part.index === selectedPart)) selectedPart = (choices.find(part => part.available) || choices[0] || {}).index;
    const shortcut = choices.find(part => part.index === selectedPart);
    notifyFocus(bench.selected, shortcut ? shortcut.index : -1);
    const installing = choices.some(part => part.installing);
    status.textContent = selected ? (bench.ready || installing ? '桌上' : '放置中') : '';
    const partKey = bench.installKey || (bench.gamepad ? 'Gamepad_FaceButton_Bottom' : 'F');
    while (partCards.length > choices.length) parts.removeChild(partCards.pop().element);
    choices.forEach((part, index) => {
      const card = partCards[index] || (partCards[index] = createCard());
      card.part = part;
      card.element.className = 'bench-part' + (part === shortcut ? ' bench-part-selected' : '');
      card.button.disabled = !part.available;
      card.icon.style.setProperty('background-image', part.icon ? 'url(' + part.icon + '_Part.png)' : 'none');
      card.name.textContent = part.name;
      card.description.textContent = part.description;
      card.description.style.display = part.description ? 'block' : 'none';
      // 效果独立于简介，长列表在自己的区域滚动。安装按钮放在区域外，阅读和滚动不会触发安装。
      const effects = (part.effects || []).filter(effect => effect.text && effect.text.trim());
      card.effects.style.display = effects.length ? 'block' : 'none';
      while (card.effectRows.length > effects.length) card.effects.removeChild(card.effectRows.pop());
      effects.forEach((effect, index) => {
        const row = card.effectRows[index] || (card.effectRows[index] = add(card.effects, 'div', 'bench-part-effect'));
        row.textContent = effect.text;
        const tone = effect.tone === 'Positive' ? 'positive' : effect.tone === 'Negative' ? 'negative' : 'neutral';
        row.className = 'bench-part-effect bench-effect-' + tone;
      });
      card.label.textContent = part.installed ? '已安装' : part.installing ? '安装中…' : part.available ? '安装' : '不可兼容 / 忙碌';
      card.key.style.display = part === shortcut ? 'flex' : 'none';
      if (part === shortcut && card.cap !== partKey) {
        setCap(card.key, partKey, partKey);
        card.cap = partKey;
      }
    });
    exitText.textContent = bench.selected < 0 ? '离开' : '取枪并离开';
    const capId = [bench.gamepad, key.key, key.label].join('|');
    if (caps !== capId) {
      caps = capId;
      setCap(previous, bench.gamepad ? 'Gamepad_LeftShoulder' : 'ArrowLeft', '‹');
      setCap(next, bench.gamepad ? 'Gamepad_RightShoulder' : 'ArrowRight', '›');
      if (key.key) setCap(exitKey, key.key, key.label);
      exitKey.style.display = key.key ? 'flex' : 'none';
    }
    previous.disabled = !bench.ready || bench.previous < 0;
    next.disabled = !bench.ready || bench.next < 0;
    previous.style.opacity = previous.disabled ? '0.32' : '1';
    next.style.opacity = next.disabled ? '0.32' : '1';
    const order = bench.weapons.map(weapon => weapon.slot).join(',');
    if (order !== slotOrder) {
      slotOrder = order;
      while (slots.firstChild) slots.removeChild(slots.firstChild);
      for (const weapon of bench.weapons) {
        const button = add(slots, 'button', 'bench-slot');
        add(button, 'span', 'bench-mark');
        button.addEventListener('click', () => select(weapon.slot));
      }
    }
    let button = slots.firstChild;
    for (const weapon of bench.weapons) {
      button.disabled = !bench.ready || weapon.slot === bench.selected;
      button.className = 'bench-slot' + (weapon.slot === bench.selected ? ' bench-current' : '');
      button.setAttribute('aria-label', weapon.name);
      button = button.nextSibling;
    }
  };

  // 原生控制器只转发导航意图；最终安装仍回传槽号与配件下标，由服务器复核兼容性。
  engine.on('benchNavigate', command => {
    if (!currentBench || !currentBench.ready) return;
    const cmd = String(command || '').toLowerCase();
    const all = currentBench.parts || [];
    const mounts = [...new Set(all.map(mountKey))];
    const choices = all.filter(part => mountKey(part) === selectedMount);
    if (cmd === 'install') { install(choices.find(part => part.index === selectedPart)); return; }
    if (cmd === 'previousmount' || cmd === 'nextmount') {
      if (!mounts.length) return;
      chooseMount(mounts[(mounts.indexOf(selectedMount) + (cmd === 'nextmount' ? 1 : -1) + mounts.length) % mounts.length]);
    } else if (cmd === 'previouspart' || cmd === 'nextpart') {
      if (!choices.length) return;
      choosePart(choices[(choices.findIndex(part => part.index === selectedPart) + (cmd === 'nextpart' ? 1 : -1) + choices.length) % choices.length].index);
    }
  });

  // 进桌时页面立即获得焦点；安装和左右切枪由页面消费，其余按键继续交给玩家控制器。
  document.addEventListener('keydown', e => {
    if (currentBench && currentBench.parts && e.key && e.key.toUpperCase() === currentBench.installKey) {
      e.preventDefault();
      if (!e.repeat) install(currentBench.parts.find(part => part.index === selectedPart));
      return;
    }
    if (!currentBench || (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight')) return;
    e.preventDefault();
    if (!e.repeat) select(e.key === 'ArrowRight' ? currentBench.next : currentBench.previous);
  });
  engine.on('interaction', message => {
    updateBench(message.bench, message.key);
    const seen = new Set();
    const keyId = (message.key && message.key.key) || '';
    for (const item of message.items) {
      seen.add(item.id);
      const state = item.pending ? 'pending' : item.state.toLowerCase();
      let entry = items.get(item.id);
      if (!entry) {
        entry = create(item.id);
        items.set(item.id, entry);
      }
      // 键只在换了（换设备、改键、新建）时重画；类在同一次执行里定好，新建的元素第一帧就是最后的样子
      if (entry.keyId !== keyId) {
        entry.keyClass = drawKey(entry.key, message.key);
        entry.keyId = keyId;
      }
      const className = 'ix ix-' + state + (item.flash ? ' ix-flash' : '') + (item.leaving ? ' ix-ghost' : '') + entry.keyClass;
      if (entry.element.className !== className) entry.element.className = className;
      const text = item.flash || (item.state === 'Blocked' ? item.reason : item.text);
      if (entry.plate.textContent !== text) entry.plate.textContent = text;
    }
    for (const [id, entry] of items) {
      if (!seen.has(id)) {
        root.removeChild(entry.element);
        items.delete(id);
      }
    }
  });
})();
