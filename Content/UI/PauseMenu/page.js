// 局内暂停菜单（UI/GinkgoPauseMenu）：左侧停靠、体积暗黑云雾、零框极简排版与二次确认弹窗。
//
// 游戏发：
//   "pause-state"：{fromGamepad, toggleKey, isPaused, isStandalone, hasCheckpoint, locationName, mapName}
//   "pause-return"：{}（从设置子页退回时刷新焦点并播放轻快回场过渡）
//   "pause-closing"：{}（C++ 启动退场模糊过渡时同步播放菜单退场动画）
// 页面发：
//   ready、sound、log、resume、openSettings {fromGamepad}、restartCheckpoint、quitGame。
(() => {
  const engine = weft.engine;
  const root = document.getElementById('root');
  const now = () => performance.now();

  const el = (parent, tag, cls, text) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined && text !== null) node.textContent = String(text);
    if (parent) parent.appendChild(node);
    return node;
  };

  const clear = node => {
    while (node.firstChild) node.removeChild(node.firstChild);
  };

  const sound = kind => engine.send('sound', kind);

  const state = {
    fromGamepad: false,
    device: 'keyboard',
    isPaused: true,
    isStandalone: true,
    hasCheckpoint: false,
    locationName: '',
    mapName: '',
    difficultyLabel: '',
    focusedId: 'resume',
    modal: null,
    closing: false,
  };

  const ITEM_STRIDE = 54; // 48px 行高 + 6px 下间距

  const getItems = () => [
    {
      id: 'resume',
      label: '继续游戏',
      enabled: true,
      danger: false,
      tag: '',
      caption: '收起菜单，立即重返行动。',
    },
    {
      id: 'checkpoint',
      label: '回到上一个检查点',
      enabled: state.hasCheckpoint,
      danger: false,
      tag: state.hasCheckpoint ? '' : '无存档',
      caption: state.hasCheckpoint
        ? (state.locationName
            ? `重返「${state.locationName}」的最近存档点，此后的未保存进度将会重置。`
            : '回到最近的自动存档点，此后的未保存进度将会重置。')
        : '尚未抵达任何存档点，目前无法回退进度。',
    },
    {
      id: 'mainMenu',
      label: '回到主菜单',
      enabled: true,
      danger: false,
      tag: '',
      caption: '离开当前战场并返回标题画面。',
    },
    {
      id: 'settings',
      label: '设置',
      enabled: true,
      danger: false,
      tag: '',
      caption: '调整操控灵敏度、按键布局、辅助瞄准与画面表现。',
    },
    {
      id: 'quit',
      label: '退出游戏',
      enabled: true,
      danger: true,
      tag: '',
      caption: '结束本次游玩并退出至桌面。',
    },
  ];

  // ===== DOM 骨架 =====

  root.className = 'pm-entering';
  el(root, 'div', 'pm-cloud');
  const stage = el(root, 'div', 'pm-stage');
  const dock = el(stage, 'div', 'pm-dock');

  const header = el(dock, 'div', 'pm-header');
  const eyebrow = el(header, 'div', 'pm-eyebrow');
  el(eyebrow, 'div', 'pm-eyebrow-gem');
  el(eyebrow, 'div', '', 'CASE GINKGO · 银杏方案');
  const titleEl = el(header, 'div', 'pm-title', '暂停菜单');
  el(header, 'div', 'pm-hairline');
  const subtitleEl = el(header, 'div', 'pm-subtitle', '');

  const center = el(dock, 'div', 'pm-center');
  const diamond = el(center, 'div', 'pm-diamond');
  el(diamond, 'div', 'pm-diamond-outer');
  el(diamond, 'div', 'pm-diamond-core');
  const listEl = el(center, 'div', 'pm-list');
  const captionEl = el(center, 'div', 'pm-caption', '');

  const footerEl = el(dock, 'div', 'pm-footer');

  const right = el(stage, 'div', 'pm-right');
  const badge = el(right, 'div', 'pm-paused-badge');
  el(badge, 'div', 'pm-paused-dot');
  const badgeText = el(badge, 'div', '', 'PAUSED');

  const noticeEl = el(root, 'div', 'pm-notice', '');

  // 二次确认弹窗
  const scrimEl = el(root, 'div', 'pm-modal-scrim');
  const modalCard = el(scrimEl, 'div', 'pm-modal');
  const modalHead = el(modalCard, 'div', 'pm-modal-head');
  const modalGem = el(modalHead, 'div', 'pm-modal-gem');
  const modalTitle = el(modalHead, 'div', 'pm-modal-title', '');
  el(modalCard, 'div', 'pm-modal-rule');
  const modalBody = el(modalCard, 'div', 'pm-modal-body', '');
  const modalActions = el(modalCard, 'div', 'pm-modal-actions');
  const modalCancelBtn = el(modalActions, 'div', 'pm-btn', '取消');
  const modalConfirmBtn = el(modalActions, 'div', 'pm-btn pm-btn-primary', '确认');

  const rowNodes = new Map();
  let noticeUntil = 0;
  let shakeRow = null;
  let shakeUntil = 0;
  let modalCloseUntil = 0;
  let animUntil = now() + 280;

  const startExit = () => {
    if (state.closing) return;
    state.closing = true;
    animUntil = 0;
    root.className = 'pm-exiting';
  };

  const requestResume = () => {
    if (state.closing) return;
    startExit();
    engine.send('resume');
  };

  const showNotice = text => {
    noticeEl.textContent = text;
    noticeEl.className = 'pm-notice pm-notice-show';
    noticeUntil = now() + 2400;
  };

  const renderFooter = () => {
    clear(footerEl);
    const addHint = (capCls, capText, label) => {
      const h = el(footerEl, 'div', 'pm-hint');
      el(h, 'div', capCls, capText);
      el(h, 'div', '', label);
    };
    if (state.device === 'gamepad') {
      addHint('pm-cap-pad', 'A', '选择');
      addHint('pm-cap-pad', 'B', '返回');
    } else {
      addHint('pm-cap-kb', 'ENTER', '选择');
      addHint('pm-cap-kb', 'ESC', '返回');
    }
  };

  const renderModal = () => {
    if (!state.modal) {
      if (modalCloseUntil <= 0 && scrimEl.className !== 'pm-modal-scrim') {
        scrimEl.className = 'pm-modal-scrim';
      }
      return;
    }
    modalCloseUntil = 0;
    const m = state.modal;
    modalTitle.textContent = m.title;
    modalBody.textContent = m.body;
    modalConfirmBtn.textContent = m.confirmLabel;
    modalGem.className = m.danger ? 'pm-modal-gem pm-modal-gem-danger' : 'pm-modal-gem';
    const cancelFocus = m.focus === 'cancel' && state.device !== 'mouse';
    const confirmFocus = m.focus === 'confirm' && state.device !== 'mouse';
    modalCancelBtn.className = `pm-btn${cancelFocus ? ' pm-btn-focus' : ''}`;
    modalConfirmBtn.className = `pm-btn ${m.danger ? 'pm-btn-danger' : 'pm-btn-primary'}${confirmFocus ? ' pm-btn-focus' : ''}`;
    if (scrimEl.className !== 'pm-modal-scrim pm-modal-open') {
      scrimEl.className = 'pm-modal-scrim pm-modal-open';
    }
  };

  const renderMenu = () => {
    const items = getItems();
    titleEl.textContent = state.locationName ? state.locationName : '暂停菜单';
    const diffPrefix = state.difficultyLabel ? `${state.difficultyLabel}难度 · ` : '';
    if (state.isStandalone) {
      subtitleEl.textContent = state.hasCheckpoint
        ? `${diffPrefix}已自动保存进度`
        : `${diffPrefix}尚未抵达首个存档点`;
    } else {
      subtitleEl.textContent = state.hasCheckpoint
        ? `${diffPrefix}已自动保存进度 · 联机进行中`
        : `${diffPrefix}联机进行中`;
    }
    badgeText.textContent = state.isPaused ? 'PAUSED' : 'MENU';

    let focusIdx = items.findIndex(item => item.id === state.focusedId);
    if (focusIdx < 0) {
      focusIdx = 0;
      state.focusedId = items[0].id;
    }

    clear(listEl);
    rowNodes.clear();

    items.forEach(item => {
      const focused = item.id === state.focusedId;
      const cls = 'pm-item'
        + (focused ? ' pm-item-focused' : '')
        + (!item.enabled ? ' pm-item-disabled' : '')
        + (item.danger ? ' pm-item-danger' : '');
      const row = el(listEl, 'div', cls);
      el(row, 'div', 'pm-item-glow');
      el(row, 'div', 'pm-item-label', item.label);
      if (item.tag) el(row, 'div', 'pm-item-tag', item.tag);

      row.addEventListener('mouseenter', () => {
        if (state.closing || state.modal) return;
        if (state.focusedId !== item.id) {
          state.focusedId = item.id;
          sound('Focus');
          renderMenu();
        }
      });
      row.addEventListener('click', () => {
        if (state.closing || state.modal) return;
        state.focusedId = item.id;
        triggerItem(item);
      });
      rowNodes.set(item.id, row);
    });

    const active = items[focusIdx];
    diamond.style.transform = `translateY(${focusIdx * ITEM_STRIDE}px)`;
    diamond.className = 'pm-diamond'
      + (!active.enabled ? ' pm-diamond-muted' : active.danger ? ' pm-diamond-danger' : '');
    captionEl.textContent = active.caption;

    renderFooter();
    renderModal();
  };

  const openModal = spec => {
    if (state.closing) return;
    sound('Select');
    modalCloseUntil = 0;
    state.modal = {...spec, focus: 'cancel'};
    renderModal();
  };

  const closeModal = silent => {
    if (!state.modal) return;
    // 立即清空逻辑态，保证玩家快速连按两次 Esc 时第二次可直接收起菜单而不会被动画吞键
    state.modal = null;
    if (!silent) {
      sound('Deny');
      modalCloseUntil = now() + 135;
      scrimEl.className = 'pm-modal-scrim pm-modal-closing';
    } else {
      modalCloseUntil = 0;
      scrimEl.className = 'pm-modal-scrim';
    }
  };

  const confirmModal = () => {
    if (!state.modal || state.closing) return;
    const onConfirm = state.modal.onConfirm;
    closeModal(true);
    if (onConfirm) onConfirm();
  };

  modalCancelBtn.addEventListener('click', () => closeModal(false));
  modalConfirmBtn.addEventListener('click', () => confirmModal());

  const triggerItem = item => {
    if (state.closing) return;
    if (!item.enabled) {
      sound('Invalid');
      const row = rowNodes.get(item.id);
      if (row) {
        if (shakeRow && shakeRow !== row) {
          shakeRow.className = shakeRow.className.replace(' pm-item-shake', '');
        }
        row.className = row.className.replace(' pm-item-shake', '') + ' pm-item-shake';
        shakeRow = row;
        shakeUntil = now() + 260;
      }
      showNotice(item.id === 'mainMenu' ? '「回到主菜单」暂未开放' : '当前没有可用的检查点存档');
      return;
    }
    if (item.id === 'resume') {
      requestResume();
    } else if (item.id === 'checkpoint') {
      openModal({
        title: '回到上一个检查点？',
        body: state.locationName
          ? `将从「${state.locationName}」的最近存档点重新开始，此后的未保存进度将会丢失。`
          : '将从最近的自动存档点重新开始，此后的未保存进度将会丢失。',
        confirmLabel: '读取存档',
        danger: false,
        onConfirm: () => engine.send('restartCheckpoint'),
      });
    } else if (item.id === 'mainMenu') {
      openModal({
        title: '返回主菜单？',
        body: '自上次自动存档以来的进度将不会保存。确定要返回主菜单吗？',
        confirmLabel: '返回主菜单',
        danger: false,
        onConfirm: () => engine.send('returnToMainMenu'),
      });
    } else if (item.id === 'settings') {
      animUntil = 0;
      root.className = '';
      sound('Select');
      engine.send('openSettings', {fromGamepad: state.device === 'gamepad'});
    } else if (item.id === 'quit') {
      openModal({
        title: '退出游戏',
        body: '自上次自动存档以来的进度将不会保存。确定要退出「银杏方案」吗？',
        confirmLabel: '退出游戏',
        danger: true,
        onConfirm: () => engine.send('quitGame'),
      });
    }
  };

  // ===== 输入：键盘 + 手柄（spatialNavigation + 轮询 Start 键防连触） =====

  const padActive = () => {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (let i = 0; i < pads.length; ++i) {
      const pad = pads[i];
      if (!pad) continue;
      if (pad.buttons.some(b => b.pressed)) return true;
      if (Math.abs(pad.axes[0]) > 0.5 || Math.abs(pad.axes[1]) > 0.5) return true;
    }
    return false;
  };

  const setDevice = dev => {
    if (state.device === dev) return;
    state.device = dev;
    renderFooter();
    renderModal();
  };

  let lastPointerX = -1;
  let lastPointerY = -1;
  document.addEventListener('mousemove', e => {
    if (lastPointerX >= 0 && (Math.abs(e.clientX - lastPointerX) > 2 || Math.abs(e.clientY - lastPointerY) > 2)) {
      setDevice('mouse');
    }
    lastPointerX = e.clientX;
    lastPointerY = e.clientY;
  });

  document.addEventListener('keydown', e => {
    const k = e.key;
    const isNav = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'w', 'W', 's', 'S', 'a', 'A', 'd', 'D', 'Enter', ' ', 'Escape', 'F10'].includes(k);
    if (!isNav) return;
    e.preventDefault();
    if (state.closing) return;
    setDevice(padActive() ? 'gamepad' : 'keyboard');

    if (k === 'Escape' || k === 'F10') {
      if (e.repeat) return;
      if (state.modal) closeModal(false);
      else requestResume();
      return;
    }

    if (state.modal) {
      if (k === 'ArrowLeft' || k === 'a' || k === 'A') {
        if (state.modal.focus !== 'cancel') {
          state.modal.focus = 'cancel';
          sound('Focus');
          renderModal();
        }
      } else if (k === 'ArrowRight' || k === 'd' || k === 'D') {
        if (state.modal.focus !== 'confirm') {
          state.modal.focus = 'confirm';
          sound('Focus');
          renderModal();
        }
      } else if ((k === 'Enter' || k === ' ') && !e.repeat) {
        if (state.modal.focus === 'confirm') confirmModal();
        else closeModal(false);
      }
      return;
    }

    const items = getItems();
    const idx = items.findIndex(item => item.id === state.focusedId);
    if (k === 'ArrowDown' || k === 's' || k === 'S') {
      state.focusedId = items[(idx + 1) % items.length].id;
      sound('Focus');
      renderMenu();
    } else if (k === 'ArrowUp' || k === 'w' || k === 'W') {
      state.focusedId = items[(idx - 1 + items.length) % items.length].id;
      sound('Focus');
      renderMenu();
    } else if ((k === 'Enter' || k === ' ') && !e.repeat) {
      triggerItem(items[idx]);
    }
  });

  const PAD_START = 9;
  let padHeld = null;
  let openedAt = now();

  const loop = () => {
    const t = now();
    if (noticeUntil > 0 && t >= noticeUntil) {
      noticeUntil = 0;
      noticeEl.className = 'pm-notice';
    }
    if (shakeUntil > 0 && t >= shakeUntil) {
      shakeUntil = 0;
      if (shakeRow) {
        shakeRow.className = shakeRow.className.replace(' pm-item-shake', '');
        shakeRow = null;
      }
    }
    if (modalCloseUntil > 0 && t >= modalCloseUntil) {
      modalCloseUntil = 0;
      if (!state.modal) scrimEl.className = 'pm-modal-scrim';
    }
    if (animUntil > 0 && t >= animUntil) {
      animUntil = 0;
      if (!state.closing) root.className = '';
    }
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let pad = null;
    for (let i = 0; i < pads.length && !pad; ++i) pad = pads[i];
    if (pad) {
      const pressed = pad.buttons.map(b => b.pressed);
      if (padHeld === null) {
        padHeld = pressed.map((held, index) => held && index === PAD_START && now() - openedAt < 1000);
      }
      if (!state.closing) {
        if (pressed.some((p, i) => p && !padHeld[i])) {
          setDevice('gamepad');
        }
        if (pressed[PAD_START] && !padHeld[PAD_START]) {
          if (state.modal) closeModal(false);
          else requestResume();
        }
      }
      padHeld = pressed;
    }
    requestAnimationFrame(loop);
  };

  // ===== 游戏事件 =====

  engine.on('pause-state', data => {
    openedAt = now();
    padHeld = null;
    state.closing = false;
    state.fromGamepad = !!data.fromGamepad;
    state.device = data.fromGamepad ? 'gamepad' : 'keyboard';
    state.isPaused = !!data.isPaused;
    state.isStandalone = !!data.isStandalone;
    state.hasCheckpoint = !!data.hasCheckpoint;
    state.locationName = data.locationName || '';
    state.mapName = data.mapName || '';
    state.difficultyLabel = data.difficultyLabel || '';
    renderMenu();
  });

  engine.on('pause-return', () => {
    openedAt = now();
    padHeld = null;
    state.closing = false;
    state.focusedId = 'settings';
    animUntil = now() + 240;
    root.className = 'pm-returning';
    renderMenu();
  });

  engine.on('pause-closing', startExit);

  renderMenu();
  requestAnimationFrame(loop);
  engine.send('ready');
})();
