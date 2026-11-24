// 特性画廊视图组件（Content/UI/MainMenu/gallery_views.js）
// 提供：
//   1. Key Code Previewer（无行号 C++ 轻量语法高亮预览器，支持多片段切换）
//   2. KaTeX 公式区（复用 Theme/paper.js 的 Paper.math 与 Settings 同源 KaTeX 0.16.47 字体）
//   3. 多态设计结构面板（pipeline / specs / tracks / states / layers）
//   4. 20 幅定制 640×212 矢量技术示意图（严格遵循 Weft SVG 约束：无 viewBox、无元素 opacity）
(() => {
  const SVG_NS = 'http://www.w3.org/2000/svg';

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

  const svgEl = (parent, tag, attrs) => {
    const node = document.createElementNS(SVG_NS, tag);
    if (attrs) {
      for (const [k, v] of Object.entries(attrs)) {
        node.setAttribute(k, String(v));
      }
    }
    if (parent) parent.appendChild(node);
    return node;
  };

  // ==================== 1. C++ 关键代码语法高亮预览器（无行号） ====================

  const CPP_KEYWORDS = new Set([
    'void', 'const', 'constexpr', 'if', 'else', 'for', 'while', 'return', 'continue', 'break',
    'double', 'float', 'bool', 'int32', 'uint8', 'auto', 'template', 'typename', 'static_cast',
    'true', 'false', 'nullptr', 'this', 'switch', 'case', 'struct', 'class', 'namespace',
  ]);

  const CPP_TYPES = new Set([
    'FVector', 'FVector2D', 'FQuat', 'FRotator', 'FHitResult', 'FMath', 'FLinearColor',
    'FSoftObjectPath', 'FGameplayTag', 'FName', 'TArray', 'TInlineAllocator', 'TGuardValue', 'TWeakObjectPtr',
    'Ginkgo', 'Movement', 'Animation', 'Occupancy', 'Scope', 'Weapon', 'Combat', 'Camera', 'Input',
    'AnimationCore', 'FMovementTags', 'EGinkgoWindowEvent', 'EGinkgoWindowRepush',
    'EGinkgoAnimActionPhase', 'EGinkgoAnimActionOutcome', 'EGinkgoCameraBlend', 'EGinkgoResponseCurve',
    'EMontageEnd', 'FCommand', 'FPathPoints', 'FLeanChain', 'EAllowShrinking',
    'FRHICommandList', 'ERDGPassFlags', 'ERenderTargetLoadAction', 'ERHIAccess',
    'FCollisionShape', 'ECC_Camera', 'ECC_Visibility',
    'UE_KINDA_SMALL_NUMBER', 'UE_SMALL_NUMBER', 'INDEX_NONE', 'NAME_None', 'TEXT', 'RDG_EVENT_NAME',
  ]);

  const isTypeLike = word => (
    CPP_TYPES.has(word)
    || /^(U|A|F|E|I|T)Ginkgo[A-Za-z0-9_]*$/.test(word)
    || /^(F|E|U|A)Weft[A-Za-z0-9_]*$/.test(word)
    || /^(Ability|Effect|Movement|Occupancy)_[A-Za-z0-9_]+$/.test(word)
  );

  const tokenizeCppLine = (parent, line) => {
    const m = /^(\s*)(.*)$/.exec(line);
    const indent = m ? m[1].replace(/\t/g, '  ').length : 0;
    const text = m ? m[2] : line;
    if (!text) {
      // 纯几何空行占位（不放文本字符，避免行高大于 8px 触发 Weft text overflow reference）
      el(parent, 'div', 'mm-code-gap');
      return;
    }
    const row = el(parent, 'div', 'mm-code-line');
    if (indent > 0) row.style.paddingLeft = `${indent * 7.4}px`;

    // 按优先级扫描：单行注释 | 字符串 | 数字 | 标识符 | 标点与空白
    const rx = /(\/\/.*$)|("(?:\\.|[^"\\])*")|(\b\d+(?:\.\d*)?f?\b)|([A-Za-z_][A-Za-z0-9_]*)|([^A-Za-z0-9_"/]+|\/)/g;
    let match;
    while ((match = rx.exec(text))) {
      const [full, com, str, num, ident, other] = match;
      if (com) {
        el(row, 'span', 'mm-tok-com', com);
      } else if (str) {
        el(row, 'span', 'mm-tok-str', str);
      } else if (num) {
        el(row, 'span', 'mm-tok-num', num);
      } else if (ident) {
        let cls = 'mm-tok-id';
        if (CPP_KEYWORDS.has(ident)) {
          cls = 'mm-tok-kw';
        } else if (isTypeLike(ident)) {
          cls = 'mm-tok-type';
        } else {
          const nextCh = text[match.index + full.length];
          if (nextCh === '(') cls = 'mm-tok-fn';
        }
        el(row, 'span', cls, ident);
      } else if (other) {
        // 用不换行空格保留运算符之间真实间距
        el(row, 'span', 'mm-tok-op', other.replace(/ /g, '\u00A0'));
      }
    }
  };

  const renderCodePreviewer = (host, snippets, activeIdx, onSelectSnippet) => {
    clear(host);
    if (!snippets || !snippets.length) return;
    const idx = Math.max(0, Math.min(activeIdx || 0, snippets.length - 1));
    const snip = snippets[idx];

    const bar = el(host, 'div', 'mm-code-bar');
    const barLeft = el(bar, 'div', 'mm-code-bar-left');
    el(barLeft, 'div', 'mm-code-lang', 'C++');
    el(barLeft, 'div', 'mm-code-file', snip.file);

    const barRight = el(bar, 'div', 'mm-code-bar-right');
    if (snippets.length > 1) {
      snippets.forEach((s, i) => {
        const pill = el(barRight, 'div', `mm-code-tab${i === idx ? ' mm-code-tab-active' : ''}`, `片段 ${i + 1}`);
        pill.addEventListener('click', () => {
          if (i !== idx && onSelectSnippet) onSelectSnippet(i);
        });
      });
    }

    const sub = el(host, 'div', 'mm-code-sub');
    el(sub, 'div', 'mm-code-title', snip.title);
    el(sub, 'div', 'mm-code-symbol', snip.symbol);

    const body = el(host, 'div', 'mm-code-body');
    const lines = String(snip.code || '').split('\n');
    lines.forEach(line => tokenizeCppLine(body, line));
  };

  // ==================== 2. KaTeX 公式面板（与 Settings 共用 Paper.math） ====================

  const renderFormulas = (host, formulas) => {
    clear(host);
    if (!formulas || !formulas.length) return;
    formulas.forEach((item, idx) => {
      const card = el(host, 'div', `mm-formula-card${idx === formulas.length - 1 ? ' mm-formula-card-last' : ''}`);
      if (item.label) el(card, 'div', 'mm-formula-label', item.label);
      const texList = Array.isArray(item.lines) ? item.lines : (item.tex ? [item.tex] : []);
      texList.forEach(tex => {
        const line = el(card, 'div', 'mm-formula-line');
        if (globalThis.Paper && typeof Paper.math === 'function') {
          const mode = Paper.math(line, tex, {display: true, size: texList.length > 1 ? 15 : 16});
          if (mode !== 'katex' && globalThis.weft && weft.engine) {
            weft.engine.send('log', `Gallery KaTeX fallback (${item.label || ''}): ${tex}`);
          }
        } else {
          el(line, 'div', 'mm-formula-fallback', tex);
        }
      });
      if (item.note) el(card, 'div', 'mm-formula-note', item.note);
    });
  };

  // ==================== 3. 多态设计结构面板 ====================

  const renderStructure = (host, structure) => {
    clear(host);
    if (!structure) return;
    el(host, 'div', 'mm-struct-head', structure.title || '关键设计结构');

    if (structure.kind === 'specs') {
      const grid = el(host, 'div', 'mm-spec-grid');
      structure.items.forEach((it, idx) => {
        const cell = el(grid, 'div', `mm-spec-cell${idx % 2 === 1 ? ' mm-spec-cell-r' : ''}`);
        el(cell, 'div', 'mm-spec-k', it.k);
        el(cell, 'div', 'mm-spec-v', it.v);
        if (it.note) el(cell, 'div', 'mm-spec-note', it.note);
      });
      return;
    }

    if (structure.kind === 'pipeline') {
      const grid = el(host, 'div', 'mm-pipe-grid');
      structure.stages.forEach((st, idx) => {
        const box = el(grid, 'div', `mm-pipe-step${idx % 2 === 1 ? ' mm-pipe-step-r' : ''}`);
        const top = el(box, 'div', 'mm-pipe-top');
        el(top, 'div', 'mm-pipe-num', st.n);
        el(top, 'div', 'mm-pipe-name', st.name);
        el(box, 'div', 'mm-pipe-detail', st.detail);
      });
      return;
    }

    if (structure.kind === 'tracks') {
      const list = el(host, 'div', 'mm-track-list');
      structure.rows.forEach(r => {
        const row = el(list, 'div', `mm-track-row mm-track-${r.color || 'gold'}`);
        const left = el(row, 'div', 'mm-track-left');
        el(left, 'div', 'mm-track-name', r.name);
        el(left, 'div', 'mm-track-meta', `${r.push} · ${r.phase}`);
        el(row, 'div', 'mm-track-effect', r.effect);
      });
      return;
    }

    if (structure.kind === 'states') {
      const list = el(host, 'div', 'mm-state-list');
      structure.items.forEach(s => {
        const row = el(list, 'div', 'mm-state-row');
        el(row, 'div', 'mm-state-pill', s.state);
        el(row, 'div', 'mm-state-role', s.role);
        el(row, 'div', 'mm-state-next', s.next);
      });
      return;
    }

    if (structure.kind === 'layers') {
      const list = el(host, 'div', 'mm-layer-list');
      structure.layers.forEach(ly => {
        const row = el(list, 'div', 'mm-layer-row');
        el(row, 'div', 'mm-layer-idx', ly.idx);
        el(row, 'div', `mm-layer-tag mm-layer-tag-${ly.tag.toLowerCase()}`, ly.tag);
        const info = el(row, 'div', 'mm-layer-info');
        el(info, 'div', 'mm-layer-name', ly.name);
        el(info, 'div', 'mm-layer-detail', ly.detail);
      });
    }
  };

  // ==================== 4. 15 幅定制矢量示意图（640 × 212 逻辑 px） ====================

  const W = 640;
  const H = 212;
  const GOLD = '#e5b443';
  const PALE = '#e8cf9c';
  const INK = '#fffbf4';
  const JADE = '#5fe08a';
  const PURPLE = '#a68ce0';
  const DANGER = '#e0654a';
  const GRID = 'rgba(255, 251, 244, 0.06)';
  const AXIS = 'rgba(255, 251, 244, 0.34)';

  const addTag = (host, x, y, text, cls = '') => {
    const d = el(host, 'div', `mm-diag-label${cls ? ' ' + cls : ''}`, text);
    d.style.left = `${x}px`;
    d.style.top = `${y}px`;
    return d;
  };

  const drawGrid = (svg, x0, y0, x1, y1, stepX, stepY) => {
    for (let x = x0; x <= x1; x += stepX) {
      svgEl(svg, 'line', {x1: x, y1: y0, x2: x, y2: y1, stroke: GRID, 'stroke-width': 1});
    }
    for (let y = y0; y <= y1; y += stepY) {
      svgEl(svg, 'line', {x1: x0, y1: y, x2: x1, y2: y, stroke: GRID, 'stroke-width': 1});
    }
  };

  const arrow = (svg, x1, y1, x2, y2, color, width = 2) => {
    svgEl(svg, 'line', {x1, y1, x2, y2, stroke: color, 'stroke-width': width, 'stroke-linecap': 'round'});
    const ang = Math.atan2(y2 - y1, x2 - x1);
    const len = 8;
    const ax1 = x2 - len * Math.cos(ang - 0.42);
    const ay1 = y2 - len * Math.sin(ang - 0.42);
    const ax2 = x2 - len * Math.cos(ang + 0.42);
    const ay2 = y2 - len * Math.sin(ang + 0.42);
    svgEl(svg, 'path', {
      d: `M${ax1.toFixed(1)} ${ay1.toFixed(1)}L${x2.toFixed(1)} ${y2.toFixed(1)}L${ax2.toFixed(1)} ${ay2.toFixed(1)}`,
      fill: 'none', stroke: color, 'stroke-width': width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round',
    });
  };

  const curvePath = (fn, x0, x1, steps, mapX, mapY) => {
    const pts = [];
    for (let i = 0; i <= steps; ++i) {
      const t = x0 + (x1 - x0) * (i / steps);
      pts.push(`${i === 0 ? 'M' : 'L'}${mapX(t).toFixed(1)} ${mapY(fn(t)).toFixed(1)}`);
    }
    return pts.join(' ');
  };

  const DIAGRAMS = {
    // 1. HL2 投影加速向量与速度模长钳制对比（地面 vs 空中）
    hl2_movement: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：投影加速几何
      const ox = 148;
      const oy = 148;
      svgEl(svg, 'circle', {cx: ox, cy: oy, r: 96, fill: 'none', stroke: 'rgba(255, 251, 244, 0.16)', 'stroke-width': 1.2, 'stroke-dasharray': '4 4'});
      svgEl(svg, 'line', {x1: ox - 18, y1: oy, x2: ox + 132, y2: oy, stroke: AXIS, 'stroke-width': 1});
      // 当前速度 v
      arrow(svg, ox, oy, ox + 78, oy - 62, 'rgba(255, 251, 244, 0.78)', 2.2);
      // 投影垂线
      svgEl(svg, 'line', {x1: ox + 78, y1: oy - 62, x2: ox + 78, y2: oy, stroke: PALE, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      // 期望方向 ŵ 与 v_add 增量
      arrow(svg, ox, oy, ox + 114, oy, JADE, 1.8);
      arrow(svg, ox + 78, oy - 62, ox + 114, oy - 62, GOLD, 2.4);
      // 合成后速度 v'
      arrow(svg, ox, oy, ox + 114, oy - 62, DANGER, 2.0);
      addTag(host, 28, 22, 'GROUND / AIR PROJECTION', 'mm-diag-title');
      addTag(host, 174, 70, 'v', 'mm-diag-ink');
      addTag(host, 232, 68, 'Δv = min(a·Δt, v_add)', 'mm-diag-gold');
      addTag(host, 252, 152, 'ŵ', 'mm-diag-jade');
      addTag(host, 122, 160, 'v·ŵ', 'mm-diag-sub');
      addTag(host, 40, 178, '地面：钳制 ‖v′‖ ≤ max(‖v‖, v_wish)', 'mm-diag-sub');

      // 右侧：摩擦曲线（v > StopSpeed 指数，v ≤ StopSpeed 线性）
      svgEl(svg, 'line', {x1: 368, y1: 168, x2: 604, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 368, y1: 42, x2: 368, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      // StopSpeed 参考线
      svgEl(svg, 'line', {x1: 368, y1: 128, x2: 604, y2: 128, stroke: 'rgba(229, 180, 67, 0.36)', 'stroke-width': 1, 'stroke-dasharray': '4 3'});
      // 落地 0.1s 宽限区
      svgEl(svg, 'rect', {x: 368, y: 42, width: 32, height: 126, fill: 'rgba(95, 224, 138, 0.10)'});
      const speedAt = t => {
        if (t <= 0.1) return 476.25;
        const u = t - 0.1;
        const tStop = Math.log(476.25 / 190.5) / 4.0;
        if (u <= tStop) return 476.25 * Math.exp(-4.0 * u);
        return Math.max(0, 190.5 - 190.5 * 4.0 * (u - tStop));
      };
      const mapX = t => 368 + (t / 0.75) * 230;
      const mapY = v => 168 - (v / 520) * 120;
      svgEl(svg, 'path', {d: curvePath(speedAt, 0, 0.72, 90, mapX, mapY), fill: 'none', stroke: GOLD, 'stroke-width': 2.2});
      addTag(host, 368, 22, 'FRICTION + 0.10S LANDING GRACE', 'mm-diag-title');
      addTag(host, 370, 146, '免摩擦', 'mm-diag-jade');
      addTag(host, 496, 112, 'StopSpeed 190.5', 'mm-diag-gold');
      addTag(host, 540, 174, 't (s)', 'mm-diag-sub');
    },

    // 2. Lurch 弦插值偏转圆 + 0.40s 衰减平台曲线
    lurch: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：等速圆弦插值
      const cx = 148;
      const cy = 148;
      const R = 92;
      svgEl(svg, 'path', {d: `M${cx} ${cy - R}A${R} ${R} 0 0 1 ${cx + R} ${cy}`, fill: 'none', stroke: 'rgba(255, 251, 244, 0.22)', 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      // 弦：从 (0, -R) 到 (R, 0)
      svgEl(svg, 'line', {x1: cx, y1: cy - R, x2: cx + R, y2: cy, stroke: 'rgba(232, 207, 156, 0.50)', 'stroke-width': 1.4});
      arrow(svg, cx, cy, cx, cy - R, 'rgba(255, 251, 244, 0.80)', 2.0);
      arrow(svg, cx, cy, cx + R, cy, JADE, 1.8);
      // k = 0.56 的结果点
      const kx = cx + R * 0.56;
      const ky = cy - R * 0.44;
      arrow(svg, cx, cy, kx, ky, GOLD, 2.5);
      svgEl(svg, 'circle', {cx: kx, cy: ky, r: 4.5, fill: GOLD});
      addTag(host, 28, 22, 'CHORD REDIRECT (90° TURN)', 'mm-diag-title');
      addTag(host, 128, 52, 'v_xy', 'mm-diag-ink');
      addTag(host, 246, 140, '‖v‖·ŵ', 'mm-diag-jade');
      addTag(host, 204, 92, 'k=0.56 → 51.8° / 71.2%', 'mm-diag-gold');
      addTag(host, 40, 178, '单次脉冲完全帧率无关，保留竖直 v_z', 'mm-diag-sub');

      // 右侧：0.40s 衰减曲线
      svgEl(svg, 'line', {x1: 374, y1: 168, x2: 604, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 374, y1: 44, x2: 374, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      const mx = t => 374 + (t / 0.45) * 224;
      const my = k => 168 - (k / 0.70) * 116;
      // 填充梯形区
      svgEl(svg, 'path', {
        d: `M${mx(0)} ${my(0)}L${mx(0)} ${my(0.56)}L${mx(0.2)} ${my(0.56)}L${mx(0.4)} ${my(0)}Z`,
        fill: 'rgba(229, 180, 67, 0.14)',
      });
      svgEl(svg, 'path', {
        d: `M${mx(0)} ${my(0.56)}L${mx(0.2)} ${my(0.56)}L${mx(0.4)} ${my(0)}`,
        fill: 'none', stroke: GOLD, 'stroke-width': 2.4,
      });
      svgEl(svg, 'circle', {cx: mx(0), cy: my(0.56), r: 4, fill: '#0b0b0c', stroke: DANGER, 'stroke-width': 2});
      svgEl(svg, 'circle', {cx: mx(0.2), cy: my(0.56), r: 3.5, fill: GOLD});
      svgEl(svg, 'circle', {cx: mx(0.4), cy: my(0), r: 3.5, fill: GOLD});
      addTag(host, 374, 22, 'EFFECTIVE STRENGTH k(t) = α·s(t)', 'mm-diag-title');
      addTag(host, 384, 52, 't=0 屏蔽起跳同帧', 'mm-diag-danger');
      addTag(host, 404, 102, '0.56 平台', 'mm-diag-gold');
      addTag(host, 462, 174, '0.20s', 'mm-diag-sub');
      addTag(host, 560, 174, '0.40s', 'mm-diag-sub');
    },

    // 3. 土狼时间悬崖窗 + 跳跃疲劳恢复曲线
    coyote: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：平台边缘 + 0.20s 土狼轨迹
      svgEl(svg, 'rect', {x: 34, y: 124, width: 122, height: 54, fill: 'rgba(255, 251, 244, 0.08)', stroke: 'rgba(255, 251, 244, 0.56)', 'stroke-width': 1.4});
      // 走下边缘自由落体弧（土狼区 0.20s 高亮）
      svgEl(svg, 'path', {d: 'M156 120Q206 122 232 146', fill: 'none', stroke: GOLD, 'stroke-width': 2.6});
      svgEl(svg, 'path', {d: 'M232 146Q254 166 268 186', fill: 'none', stroke: 'rgba(255, 251, 244, 0.28)', 'stroke-width': 1.4, 'stroke-dasharray': '4 3'});
      // 土狼起跳抛物线
      svgEl(svg, 'path', {d: 'M216 136Q260 66 312 88', fill: 'none', stroke: JADE, 'stroke-width': 2.2});
      svgEl(svg, 'circle', {cx: 156, cy: 120, r: 4, fill: INK});
      svgEl(svg, 'circle', {cx: 216, cy: 136, r: 5, fill: JADE});
      addTag(host, 34, 22, 'COYOTE 0.20S LEDGE WINDOW', 'mm-diag-title');
      addTag(host, 54, 100, 'WalkedOffLedge', 'mm-diag-sub');
      addTag(host, 170, 152, '0.20s 宽限', 'mm-diag-gold');
      addTag(host, 240, 56, '满高空中起跳 (M=1)', 'mm-diag-jade');

      // 右侧：跳跃疲劳 0.75s 曲线
      svgEl(svg, 'line', {x1: 374, y1: 168, x2: 604, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 374, y1: 48, x2: 374, y2: 168, stroke: AXIS, 'stroke-width': 1.2});
      const mx = t => 374 + (t / 0.85) * 224;
      const my = m => 168 - m * 110;
      svgEl(svg, 'path', {
        d: `M${mx(0)} ${my(0.25)}L${mx(0.15)} ${my(0.25)}L${mx(0.75)} ${my(1.0)}L${mx(0.84)} ${my(1.0)}`,
        fill: 'none', stroke: PURPLE, 'stroke-width': 2.4,
      });
      svgEl(svg, 'line', {x1: 374, y1: my(1.0), x2: 604, y2: my(1.0), stroke: 'rgba(229, 180, 67, 0.45)', 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      addTag(host, 374, 22, 'JUMP FATIGUE RECOVERY M(t)', 'mm-diag-title');
      addTag(host, 384, 44, '1.00 (Coyote 阻断惩罚)', 'mm-diag-gold');
      addTag(host, 384, 122, '0.25×', 'mm-diag-purple');
      addTag(host, 408, 174, '0.15s', 'mm-diag-sub');
      addTag(host, 560, 174, '0.75s', 'mm-diag-sub');
    },

    // 4. 滑铲解析衰减曲线 + 2.0s 疲劳窗对比
    slide_fatigue: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      svgEl(svg, 'line', {x1: 48, y1: 170, x2: 602, y2: 170, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 48, y1: 40, x2: 48, y2: 170, stroke: AXIS, 'stroke-width': 1.2});
      const mx = t => 48 + (t / 2.6) * 548;
      const my = v => 170 - (v / 1280) * 124;

      // 2.0s 疲劳条带
      svgEl(svg, 'rect', {x: mx(0), y: 40, width: mx(2.0) - mx(0), height: 130, fill: 'rgba(224, 101, 74, 0.08)'});
      svgEl(svg, 'line', {x1: mx(2.0), y1: 40, x2: mx(2.0), y2: 170, stroke: DANGER, 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      // 退出滑铲门限 317.5 cm/s
      svgEl(svg, 'line', {x1: 48, y1: my(317.5), x2: 602, y2: my(317.5), stroke: 'rgba(255, 251, 244, 0.30)', 'stroke-width': 1, 'stroke-dasharray': '3 3'});

      // 首次滑铲（带 +438.15 加速，1168.4 封顶）
      const decel = (v0, t) => {
        const off = 127.0 / 0.35667;
        return Math.max(0, (v0 + off) * Math.exp(-0.35667 * t) - off);
      };
      svgEl(svg, 'path', {d: curvePath(t => decel(1168.4, t), 0, 2.55, 110, mx, my), fill: 'none', stroke: GOLD, 'stroke-width': 2.5});
      // 疲劳内二连铲（无加速，从 762 冲刺衰减）
      svgEl(svg, 'path', {d: curvePath(t => decel(762.0, t), 0, 1.80, 90, mx, my), fill: 'none', stroke: DANGER, 'stroke-width': 1.8, 'stroke-dasharray': '6 3'});
      svgEl(svg, 'circle', {cx: mx(0), cy: my(1168.4), r: 4, fill: GOLD});
      svgEl(svg, 'circle', {cx: mx(0), cy: my(762.0), r: 3.5, fill: DANGER});
      addTag(host, 56, 20, 'EXACT ODE SLIDE VELOCITY & 2.0S SLIDE FATIGUE', 'mm-diag-title');
      addTag(host, 128, 46, '首铲 +438.2 → Cap 1168 cm/s', 'mm-diag-gold');
      addTag(host, 64, 116, '疲劳内再铲 (SlideBoost × 0)', 'mm-diag-danger');
      addTag(host, 476, 46, '2.00s 疲劳解除', 'mm-diag-danger');
      addTag(host, 512, 116, '退铲 317.5', 'mm-diag-sub');
    },

    // 5. 0-125 cm 连续攀爬曲线 + 接触段 Hermite + 独立姿势代理
    traversal: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 地面与掩体墙
      svgEl(svg, 'line', {x1: 34, y1: 174, x2: 606, y2: 174, stroke: AXIS, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 340, y: 86, width: 178, height: 88, fill: 'rgba(255, 251, 244, 0.08)', stroke: 'rgba(255, 251, 244, 0.65)', 'stroke-width': 1.5});
      // 0–50 / 100 / 125 cm 连续起步带
      svgEl(svg, 'rect', {x: 112, y: 164, width: 228, height: 10, fill: 'rgba(229, 180, 67, 0.22)'});
      svgEl(svg, 'line', {x1: 112, y1: 156, x2: 112, y2: 182, stroke: GOLD, 'stroke-width': 1.5});
      svgEl(svg, 'line', {x1: 158, y1: 160, x2: 158, y2: 180, stroke: PALE, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 249, y1: 160, x2: 249, y2: 180, stroke: PALE, 'stroke-width': 1.2});
      // 三条调和 Hermite 轨迹（冲刺 125 / 跑 100 / 静止 50）
      svgEl(svg, 'path', {d: 'M122 146C166 56 266 44 358 66C394 74 422 80 442 80', fill: 'none', stroke: GOLD, 'stroke-width': 2.4});
      svgEl(svg, 'path', {d: 'M168 146C202 68 274 54 358 70C394 76 422 80 442 80', fill: 'none', stroke: PALE, 'stroke-width': 1.6});
      svgEl(svg, 'path', {d: 'M258 146C274 88 308 72 362 76C396 78 422 80 442 80', fill: 'none', stroke: 'rgba(255, 251, 244, 0.45)', 'stroke-width': 1.3});
      // 收缩姿势碰撞代理（空中收腿）
      svgEl(svg, 'rect', {x: 318, y: 42, width: 20, height: 34, rx: 10, fill: 'rgba(95, 224, 138, 0.16)', stroke: JADE, 'stroke-width': 1.5, 'stroke-dasharray': '3 2'});
      svgEl(svg, 'circle', {cx: 340, cy: 86, r: 5.5, fill: GOLD});
      svgEl(svg, 'circle', {cx: 340, cy: 86, r: 12, fill: 'none', stroke: 'rgba(229, 180, 67, 0.45)', 'stroke-width': 1.2});
      addTag(host, 34, 20, '0–125 CM CONTINUOUS REACH + HARMONIC CONTACT HERMITE', 'mm-diag-title');
      addTag(host, 96, 182, '125cm 冲刺', 'mm-diag-gold');
      addTag(host, 152, 182, '100cm 跑', 'mm-diag-sub');
      addTag(host, 240, 182, '50cm 静止', 'mm-diag-sub');
      addTag(host, 354, 34, '2mm 收腿碰撞代理 (120Hz)', 'mm-diag-jade');
      addTag(host, 360, 100, '触壁共享调和切线 m_c', 'mm-diag-gold');
    },

    // 6. F6 Window Timeline 因果链多轨示意图
    tag_window: (host, svg) => {
      // 仿 F6 WindowTimeline 实机布局：Event 轨 + 4 窗口轨 + 3 链色因果竖线
      const laneX = 114;
      const laneW = 500;
      const nowX = laneX + laneW * 0.84;
      svgEl(svg, 'line', {x1: laneX, y1: 30, x2: laneX + laneW, y2: 30, stroke: GRID, 'stroke-width': 1});
      for (let i = 0; i < 4; ++i) {
        const y = 62 + i * 35;
        svgEl(svg, 'line', {x1: laneX, y1: y + 26, x2: laneX + laneW, y2: y + 26, stroke: GRID, 'stroke-width': 1});
      }
      // 因果竖线（金：WalkedOffLedge→Coyote；绿：Jumped→EndCoyote+PushLurch+PushJumpFatigue；紫：SlideStarted→SlideFatigue）
      const t1 = 188;
      const t2 = 278;
      const t3 = 414;
      svgEl(svg, 'line', {x1: t1, y1: 42, x2: t1, y2: 86, stroke: GOLD, 'stroke-width': 1.5});
      svgEl(svg, 'line', {x1: t2, y1: 42, x2: t2, y2: 162, stroke: JADE, 'stroke-width': 1.5});
      svgEl(svg, 'line', {x1: t3, y1: 42, x2: t3, y2: 192, stroke: PURPLE, 'stroke-width': 1.5});
      // 现在竖线
      svgEl(svg, 'line', {x1: nowX, y1: 24, x2: nowX, y2: 200, stroke: 'rgba(255, 251, 244, 0.55)', 'stroke-width': 1.2, 'stroke-dasharray': '3 3'});

      // 事件芯片
      const chip = (x, w, color, bg) => {
        svgEl(svg, 'rect', {x, y: 24, width: w, height: 18, rx: 2, fill: bg, stroke: color, 'stroke-width': 1.3});
      };
      chip(t1 - 44, 88, GOLD, '#332914');
      chip(t2 - 32, 64, JADE, '#132b20');
      chip(t3 - 42, 84, PURPLE, '#241d36');

      // 轨道段（实心已过 + 空心剩余时长）
      svgEl(svg, 'rect', {x: t1, y: 66, width: t2 - t1, height: 18, rx: 2, fill: '#332914', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: t2, y: 101, width: 104, height: 18, rx: 2, fill: '#132b20', stroke: JADE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: t2 + 104, y: 101, width: 46, height: 18, rx: 2, fill: 'none', stroke: JADE, 'stroke-width': 1.2, 'stroke-dasharray': '3 2'});
      svgEl(svg, 'rect', {x: t2, y: 136, width: nowX - t2, height: 18, rx: 2, fill: '#132b20', stroke: JADE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: t3, y: 171, width: nowX - t3, height: 18, rx: 2, fill: '#241d36', stroke: PURPLE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: nowX, y: 171, width: 72, height: 18, rx: 2, fill: 'none', stroke: PURPLE, 'stroke-width': 1.2, 'stroke-dasharray': '3 2'});

      addTag(host, 20, 26, 'EVENTS', 'mm-diag-title');
      addTag(host, 20, 68, 'CoyoteTime', 'mm-diag-gold');
      addTag(host, 20, 103, 'Lurch', 'mm-diag-jade');
      addTag(host, 20, 138, 'JumpFatigue', 'mm-diag-jade');
      addTag(host, 20, 173, 'SlideFatigue', 'mm-diag-purple');
      addTag(host, t1 - 36, 26, 'WalkOff', 'mm-diag-gold');
      addTag(host, t2 - 22, 26, 'Jumped', 'mm-diag-jade');
      addTag(host, t3 - 34, 26, 'SlideStart', 'mm-diag-purple');
      addTag(host, nowX - 14, 6, 'NOW', 'mm-diag-ink');
    },

    // 6B. 预测式地形 FootIK：支撑脚分段双上凸包虚拟地面 + 骨盆跨距基线与手根联动
    predictive_ik: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：下楼多级台阶 + 朴素弦(红虚线) vs 支撑脚锚点分段上凸包(绿) + 8 步摆动弧(金)
      svgEl(svg, 'path', {
        d: 'M34 86H104V114H174V142H244V170H324',
        fill: 'none', stroke: 'rgba(255, 251, 244, 0.62)', 'stroke-width': 1.6,
      });
      svgEl(svg, 'path', {
        d: 'M34 86H104V114H174V142H244V170H324V190H34Z',
        fill: 'rgba(255, 251, 244, 0.05)',
      });
      // 朴素起点→终点直线弦（悬空）
      svgEl(svg, 'line', {x1: 68, y1: 84, x2: 292, y2: 168, stroke: 'rgba(224, 101, 74, 0.50)', 'stroke-width': 1.3, 'stroke-dasharray': '4 3'});
      // 支撑脚锚点分段上凸包
      svgEl(svg, 'path', {d: 'M68 84L104 84L174 112L204 140L244 140L292 168', fill: 'none', stroke: JADE, 'stroke-width': 2.0});
      // 8 步预测摆动弧
      svgEl(svg, 'path', {d: 'M68 78Q142 62 204 128Q252 126 292 162', fill: 'none', stroke: GOLD, 'stroke-width': 2.4});
      svgEl(svg, 'circle', {cx: 204, cy: 140, r: 5.5, fill: PURPLE, stroke: INK, 'stroke-width': 1.4});
      svgEl(svg, 'circle', {cx: 68, cy: 84, r: 4, fill: INK});
      svgEl(svg, 'circle', {cx: 292, cy: 168, r: 4.5, fill: GOLD});
      addTag(host, 34, 18, 'SPLIT UPPER HULL VIRTUAL GROUND', 'mm-diag-title');
      addTag(host, 108, 48, '8 步外推摆动弧 (+2cm)', 'mm-diag-gold');
      addTag(host, 204, 88, '朴素弦悬空', 'mm-diag-danger');
      addTag(host, 128, 154, '支撑脚锚点分段', 'mm-diag-purple');

      // 右侧：骨盆跨距连线 + 伸展/折叠包络 + ik_hand_root 联动
      const ax = 414;
      const ay = 166;
      const bx = 562;
      const by = 118;
      svgEl(svg, 'line', {x1: ax, y1: ay, x2: bx, y2: by, stroke: 'rgba(232, 207, 156, 0.56)', 'stroke-width': 1.6, 'stroke-dasharray': '4 3'});
      // 骨盆投影与上下限带
      const px = 488;
      const pyBase = 142;
      const pyPelvis = 86;
      svgEl(svg, 'rect', {x: px - 24, y: pyPelvis - 16, width: 48, height: 34, rx: 2, fill: 'rgba(229, 180, 67, 0.12)', stroke: GOLD, 'stroke-width': 1.3});
      svgEl(svg, 'line', {x1: px, y1: pyPelvis + 18, x2: px, y2: pyBase, stroke: PALE, 'stroke-width': 1.2, 'stroke-dasharray': '2 2'});
      svgEl(svg, 'circle', {cx: px, cy: pyPelvis, r: 5, fill: GOLD});
      svgEl(svg, 'circle', {cx: ax, cy: ay, r: 4.5, fill: INK});
      svgEl(svg, 'circle', {cx: bx, cy: by, r: 4.5, fill: JADE});
      // 同步偏移 ik_hand_root
      arrow(svg, px + 24, pyPelvis, px + 76, pyPelvis - 16, PURPLE, 1.8);
      svgEl(svg, 'rect', {x: px + 78, y: pyPelvis - 26, width: 32, height: 20, rx: 2, fill: 'rgba(166, 140, 224, 0.18)', stroke: PURPLE, 'stroke-width': 1.3});
      addTag(host, 388, 18, 'PELVIS LINE & IK_HAND_ROOT SYNC', 'mm-diag-title');
      addTag(host, 396, 174, 'Foot A', 'mm-diag-sub');
      addTag(host, 554, 126, 'Foot B', 'mm-diag-jade');
      addTag(host, 442, 50, 'Pelvis (Drop≤15)', 'mm-diag-gold');
      addTag(host, 552, 42, 'ik_hand_root', 'mm-diag-purple');
    },

    // 7. FootIK 双骨/Soft IK/3 射线足底贴合
    foot_ik: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：楼梯台阶 + 上凸包虚拟地面
      svgEl(svg, 'path', {d: 'M34 176H130V146H212V116H306V176', fill: 'rgba(255, 251, 244, 0.07)', stroke: 'rgba(255, 251, 244, 0.54)', 'stroke-width': 1.4});
      // 上凸包路径
      svgEl(svg, 'path', {d: 'M66 174L130 144L212 114L274 114', fill: 'none', stroke: JADE, 'stroke-width': 2.2, 'stroke-dasharray': '5 3'});
      // 8 步摆动脚弧线（离虚拟地面 +2cm）
      svgEl(svg, 'path', {d: 'M66 168Q142 106 274 106', fill: 'none', stroke: GOLD, 'stroke-width': 2.4});
      svgEl(svg, 'circle', {cx: 66, cy: 170, r: 4.5, fill: INK});
      svgEl(svg, 'circle', {cx: 274, cy: 112, r: 5, fill: GOLD});
      addTag(host, 34, 20, 'UPPER CONVEX HULL VIRTUAL GROUND', 'mm-diag-title');
      addTag(host, 78, 88, '8 步外推摆动弧 (+2cm)', 'mm-diag-gold');
      addTag(host, 150, 152, '单调链上凸包', 'mm-diag-jade');

      // 右侧：双骨腿 + 抗翻膝极向量 + 3 射线足底贴合
      const hip = [458, 52];
      const knee = [498, 108];
      const ankle = [466, 158];
      svgEl(svg, 'line', {x1: 386, y1: 178, x2: 586, y2: 152, stroke: 'rgba(255, 251, 244, 0.56)', 'stroke-width': 1.6});
      // 3 条足底探测射线（heel / ball / toe）
      [444, 472, 502].forEach((rx, i) => {
        svgEl(svg, 'line', {x1: rx, y1: 146, x2: rx, y2: 171 - i * 4, stroke: 'rgba(229, 180, 67, 0.55)', 'stroke-width': 1.2, 'stroke-dasharray': '2 2'});
      });
      svgEl(svg, 'line', {x1: hip[0], y1: hip[1], x2: knee[0], y2: knee[1], stroke: INK, 'stroke-width': 3.2, 'stroke-linecap': 'round'});
      svgEl(svg, 'line', {x1: knee[0], y1: knee[1], x2: ankle[0], y2: ankle[1], stroke: GOLD, 'stroke-width': 3.2, 'stroke-linecap': 'round'});
      svgEl(svg, 'line', {x1: 444, y1: 168, x2: 504, y2: 160, stroke: JADE, 'stroke-width': 3.0, 'stroke-linecap': 'round'});
      arrow(svg, knee[0], knee[1], knee[0] + 42, knee[1] - 6, PURPLE, 1.8);
      [hip, knee, ankle].forEach(([x, y]) => svgEl(svg, 'circle', {cx: x, cy: y, r: 4.5, fill: '#0b0b0c', stroke: GOLD, 'stroke-width': 2}));
      addTag(host, 386, 20, 'TWO-BONE + SOFT IK + 3-RAY FOOT', 'mm-diag-title');
      addTag(host, 404, 44, 'Pelvis', 'mm-diag-ink');
      addTag(host, 542, 94, 'k_pole', 'mm-diag-purple');
      addTag(host, 514, 168, '≤30° 坡面贴合', 'mm-diag-jade');
    },

    // 8. 10 态 Locomotion FSM 拓扑图
    anim_graph: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      const node = (x, y, w, h, stroke, fill) => {
        svgEl(svg, 'rect', {x, y, width: w, height: h, rx: 3, fill, stroke, 'stroke-width': 1.4});
      };
      // 连线
      const links = [
        [106, 98, 152, 98], [222, 98, 268, 98], [338, 98, 384, 98],
        [303, 114, 303, 148], [187, 114, 268, 158], [338, 158, 404, 114],
        [72, 114, 72, 148], [460, 82, 502, 62], [460, 114, 502, 142],
      ];
      links.forEach(([x1, y1, x2, y2]) => {
        svgEl(svg, 'line', {x1, y1, x2, y2, stroke: 'rgba(232, 207, 156, 0.45)', 'stroke-width': 1.3});
      });
      // 核心地面节点
      node(34, 82, 72, 32, GOLD, 'rgba(229, 180, 67, 0.16)');
      node(34, 148, 86, 30, PALE, 'rgba(255, 251, 244, 0.05)');
      node(152, 82, 70, 32, INK, 'rgba(255, 251, 244, 0.07)');
      node(268, 82, 70, 32, GOLD, 'rgba(229, 180, 67, 0.18)');
      node(268, 148, 70, 30, DANGER, 'rgba(224, 101, 74, 0.14)');
      node(384, 82, 76, 32, INK, 'rgba(255, 251, 244, 0.07)');
      // 空中与滑铲集群
      node(492, 42, 120, 46, JADE, 'rgba(95, 224, 138, 0.12)');
      node(492, 118, 120, 62, PURPLE, 'rgba(166, 140, 224, 0.12)');

      addTag(host, 34, 18, '10-STATE LOCOMOTION FSM (DEAD BLENDING 0.2S / 0.3S)', 'mm-diag-title');
      addTag(host, 56, 90, 'Idle', 'mm-diag-gold');
      addTag(host, 42, 155, 'TurnInPlace', 'mm-diag-sub');
      addTag(host, 170, 90, 'Start', 'mm-diag-ink');
      addTag(host, 285, 90, 'Cycle', 'mm-diag-gold');
      addTag(host, 286, 155, 'Pivot', 'mm-diag-danger');
      addTag(host, 406, 90, 'Stop', 'mm-diag-ink');
      addTag(host, 504, 48, 'Air (4 态)', 'mm-diag-jade');
      addTag(host, 504, 66, 'Jump / Fall / Land', 'mm-diag-sub');
      addTag(host, 502, 126, 'Slide ×3', 'mm-diag-purple');
      addTag(host, 502, 146, 'Start / Loop / Exit', 'mm-diag-sub');
      addTag(host, 152, 48, '距离匹配 Start / Pivot / Stop', 'mm-diag-sub');
    },

    // 8B. 动画分层（Linked Layer + Slots）与腰椎份额前倾伸手 IK
    anim_layering: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：4 层分层架构与曲线保护
      const layers = [
        {y: 44, t: 'Locomotion Base (L/R Markers + FootLand)', c: 'rgba(255, 251, 244, 0.45)', bg: 'rgba(255, 251, 244, 0.05)'},
        {y: 80, t: 'ALI_WeaponLayers (spine_01 + ik_hand_root)', c: GOLD, bg: 'rgba(229, 180, 67, 0.13)'},
        {y: 116, t: 'UpperBody Slot (DoNotOverride Curves)', c: JADE, bg: 'rgba(95, 224, 138, 0.11)'},
        {y: 152, t: 'ArmL / ArmR Slots → Pass-1 & Pass-2 PBIK', c: PURPLE, bg: 'rgba(166, 140, 224, 0.12)'},
      ];
      layers.forEach((ly, i) => {
        svgEl(svg, 'rect', {x: 34, y: ly.y, width: 312, height: 26, rx: 2, fill: ly.bg, stroke: ly.c, 'stroke-width': 1.3});
        addTag(host, 44, ly.y + 5, ly.t, i === 1 ? 'mm-diag-gold' : i === 2 ? 'mm-diag-jade' : i === 3 ? 'mm-diag-purple' : 'mm-diag-ink');
        if (i < 3) arrow(svg, 190, ly.y + 26, 190, ly.y + 35, PALE, 1.4);
      });
      addTag(host, 34, 18, 'LINKED LAYERS & SLOT ISOLATION', 'mm-diag-title');

      // 右侧：腰椎份额前倾（spine_01..05: 45/35/15/5%）+ 手臂伸达目标
      const spine = [[438, 174], [446, 148], [460, 122], [478, 98], [496, 78]];
      for (let i = 0; i < spine.length - 1; ++i) {
        svgEl(svg, 'line', {x1: spine[i][0], y1: spine[i][1], x2: spine[i + 1][0], y2: spine[i + 1][1], stroke: GOLD, 'stroke-width': 3.0, 'stroke-linecap': 'round'});
      }
      // 竖直参考脊柱虚线
      svgEl(svg, 'line', {x1: 438, y1: 174, x2: 438, y2: 72, stroke: 'rgba(255, 251, 244, 0.24)', 'stroke-width': 1.2, 'stroke-dasharray': '3 3'});
      // 手臂伸达至目标
      svgEl(svg, 'line', {x1: 496, y1: 78, x2: 538, y2: 102, stroke: INK, 'stroke-width': 2.6, 'stroke-linecap': 'round'});
      svgEl(svg, 'line', {x1: 538, y1: 102, x2: 582, y2: 108, stroke: JADE, 'stroke-width': 2.6, 'stroke-linecap': 'round'});
      spine.forEach(([x, y]) => svgEl(svg, 'circle', {cx: x, cy: y, r: 3.5, fill: '#0b0b0c', stroke: GOLD, 'stroke-width': 1.6}));
      svgEl(svg, 'circle', {cx: 586, cy: 108, r: 5.5, fill: JADE});
      addTag(host, 392, 18, 'LUMBAR LEAN SHARES (≤35°)', 'mm-diag-title');
      addTag(host, 386, 152, '45% 下腰', 'mm-diag-gold');
      addTag(host, 396, 120, '35%', 'mm-diag-sub');
      addTag(host, 514, 62, 'Comfort 48cm', 'mm-diag-sub');
      addTag(host, 540, 122, 'Hand Target', 'mm-diag-jade');
    },

    // 9. AnimAction 层级占位抢占与单点 Apply 命令缓冲
    anim_action: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：优先级阶梯
      const bars = [
        {y: 48, w: 72, label: '0  Hold / Pickup', color: 'rgba(255, 251, 244, 0.36)'},
        {y: 78, w: 124, label: '3  GunAction', color: PALE},
        {y: 108, w: 158, label: '4  Throw', color: JADE},
        {y: 138, w: 194, label: '5  WeaponSwap', color: GOLD},
        {y: 168, w: 242, label: '10 Traversal', color: DANGER},
      ];
      bars.forEach(b => {
        svgEl(svg, 'rect', {x: 34, y: b.y, width: b.w, height: 20, rx: 2, fill: 'rgba(255, 251, 244, 0.06)', stroke: b.color, 'stroke-width': 1.3});
        addTag(host, 42, b.y + 2, b.label, 'mm-diag-ink');
      });
      addTag(host, 34, 20, 'OCCUPANCY PRIORITY LADDER', 'mm-diag-title');

      // 右侧：Command Buffer 单点 Apply() 隔离图
      svgEl(svg, 'rect', {x: 334, y: 46, width: 118, height: 142, rx: 3, fill: 'rgba(229, 180, 67, 0.08)', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 492, y: 46, width: 114, height: 58, rx: 3, fill: 'rgba(95, 224, 138, 0.10)', stroke: JADE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 492, y: 124, width: 114, height: 64, rx: 3, fill: 'rgba(166, 140, 224, 0.10)', stroke: PURPLE, 'stroke-width': 1.4});
      arrow(svg, 452, 76, 490, 76, GOLD, 2.0);
      arrow(svg, 452, 152, 490, 152, PURPLE, 2.0);
      addTag(host, 334, 20, 'SINGLE-POINT APPLY() & CLEANUP', 'mm-diag-title');
      addTag(host, 346, 56, 'Runner Commands', 'mm-diag-gold');
      addTag(host, 346, 80, '• PlayMontage', 'mm-diag-sub');
      addTag(host, 346, 102, '• Stop (已覆写省略)', 'mm-diag-sub');
      addTag(host, 346, 124, '• CatchUp 补发', 'mm-diag-sub');
      addTag(host, 346, 146, '• MaxRounds=16', 'mm-diag-sub');
      addTag(host, 502, 54, 'IGinkgoHost', 'mm-diag-jade');
      addTag(host, 502, 76, '单次无缝混出', 'mm-diag-sub');
      addTag(host, 502, 134, '资源释放策略', 'mm-diag-purple');
      addTag(host, 502, 156, 'Tag 立刻 / IK 冻结', 'mm-diag-sub');
    },

    // 9B. AnimActionChain 声明式步骤图、标签路由与 RequestStop 软停止
    anim_action_chain: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      const box = (x, y, w, h, stroke, fill) => {
        svgEl(svg, 'rect', {x, y, width: w, height: h, rx: 3, fill, stroke, 'stroke-width': 1.4});
      };
      // 主链路三步：Take/Open → Reload/Loop → Place/Close → Done
      box(34, 72, 106, 42, PALE, 'rgba(255, 251, 244, 0.06)');
      box(184, 72, 124, 42, GOLD, 'rgba(229, 180, 67, 0.16)');
      box(352, 72, 120, 42, JADE, 'rgba(95, 224, 138, 0.12)');
      box(516, 72, 92, 42, GOLD, 'rgba(229, 180, 67, 0.12)');
      arrow(svg, 140, 93, 182, 93, INK, 1.8);
      arrow(svg, 308, 93, 350, 93, GOLD, 1.8);
      arrow(svg, 472, 93, 514, 93, JADE, 1.8);
      // Loop 自回环弧（如 98k 续装）
      svgEl(svg, 'path', {d: 'M216 72C216 44 278 44 278 70', fill: 'none', stroke: GOLD, 'stroke-width': 1.5});
      arrow(svg, 276, 62, 278, 71, GOLD, 1.5);
      // 软停止 RequestStop 捷径（绿虚线直入关栓收尾）
      svgEl(svg, 'path', {d: 'M246 114V152H412V116', fill: 'none', stroke: JADE, 'stroke-width': 1.6, 'stroke-dasharray': '4 3'});
      arrow(svg, 412, 124, 412, 116, JADE, 1.6);
      // OnAborted 规则分支（紫）与 Cancelled 立即终止（红）
      box(34, 148, 152, 34, PURPLE, 'rgba(166, 140, 224, 0.12)');
      box(486, 148, 122, 34, DANGER, 'rgba(224, 101, 74, 0.12)');
      arrow(svg, 198, 114, 162, 146, PURPLE, 1.5);

      addTag(host, 34, 18, 'ANIMACTIONCHAIN ROUTING & SOFT REQUESTSTOP', 'mm-diag-title');
      addTag(host, 210, 34, 'Next (续装循环)', 'mm-diag-gold');
      addTag(host, 48, 84, 'Step 1: Open', 'mm-diag-ink');
      addTag(host, 198, 84, 'Step 2: Reload', 'mm-diag-gold');
      addTag(host, 366, 84, 'Step 3: Close', 'mm-diag-jade');
      addTag(host, 526, 84, 'Completed', 'mm-diag-gold');
      addTag(host, 260, 158, 'RequestStop 软切关栓', 'mm-diag-jade');
      addTag(host, 46, 156, 'On(Aborted, Tag)', 'mm-diag-purple');
      addTag(host, 498, 156, 'Cancelled → 立即停', 'mm-diag-danger');
    },

    // 10. 三环程序化相机俯仰轨道 + 两段防穿墙
    camera_pipeline: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：三环侧视剖面
      const px = 170;
      const py = 114;
      svgEl(svg, 'ellipse', {cx: px, cy: 58, rx: 48, ry: 12, fill: 'none', stroke: GOLD, 'stroke-width': 1.5});
      svgEl(svg, 'ellipse', {cx: px, cy: py, rx: 112, ry: 22, fill: 'none', stroke: 'rgba(255, 251, 244, 0.44)', 'stroke-width': 1.4});
      svgEl(svg, 'ellipse', {cx: px, cy: 170, rx: 66, ry: 14, fill: 'none', stroke: PALE, 'stroke-width': 1.4});
      // 角色胶囊与三环包络弧
      svgEl(svg, 'rect', {x: px - 10, y: py - 24, width: 20, height: 52, rx: 10, fill: 'rgba(255, 251, 244, 0.14)', stroke: INK, 'stroke-width': 1.4});
      svgEl(svg, 'path', {d: `M${px + 48} 58Q${px + 118} ${py} ${px + 66} 170`, fill: 'none', stroke: GOLD, 'stroke-width': 2.2});
      svgEl(svg, 'circle', {cx: px + 112, cy: py, r: 5, fill: GOLD});
      addTag(host, 34, 18, 'THREE-RING PITCH ORBIT', 'mm-diag-title');
      addTag(host, 224, 48, '上环 60° (0.35×)', 'mm-diag-gold');
      addTag(host, 290, 106, 'L=150cm', 'mm-diag-ink');
      addTag(host, 244, 162, '下环 45° (0.50×)', 'mm-diag-sub');

      // 右侧：两段球扫防穿墙 + 9 射线自动换肩
      const cx = 420;
      const cy = 116;
      svgEl(svg, 'rect', {x: 520, y: 46, width: 30, height: 138, fill: 'rgba(224, 101, 74, 0.16)', stroke: DANGER, 'stroke-width': 1.4});
      svgEl(svg, 'circle', {cx, cy, r: 9, fill: 'rgba(255, 251, 244, 0.18)', stroke: INK, 'stroke-width': 1.4});
      // Stage 1: Capsule → SafePivot
      arrow(svg, cx, cy, 466, 98, JADE, 2.0);
      // Stage 2: SafePivot → Wall hit（压缩）
      arrow(svg, 466, 98, 514, 98, GOLD, 2.2);
      svgEl(svg, 'line', {x1: 514, y1: 98, x2: 584, y2: 98, stroke: 'rgba(224, 101, 74, 0.50)', 'stroke-width': 1.5, 'stroke-dasharray': '4 3'});
      svgEl(svg, 'circle', {cx: 514, cy: 98, r: 8, fill: 'rgba(229, 180, 67, 0.22)', stroke: GOLD, 'stroke-width': 1.5});
      // 9 射线扇形
      [-0.45, -0.22, 0, 0.22, 0.45].forEach(a => {
        svgEl(svg, 'line', {x1: 466, y1: 146, x2: 466 + Math.cos(a) * 52, y2: 146 + Math.sin(a) * 52, stroke: 'rgba(166, 140, 224, 0.56)', 'stroke-width': 1.1});
      });
      addTag(host, 392, 18, '2-STAGE SWEEP & 9-RAY SHOULDER', 'mm-diag-title');
      addTag(host, 400, 74, '① SafePivot', 'mm-diag-jade');
      addTag(host, 550, 68, '瞬拉压缩', 'mm-diag-danger');
      addTag(host, 396, 174, '9 射线掩体评分换肩', 'mm-diag-purple');
    },

    // 10B. 相机 ParamLayer 覆盖/阻断 → Collision 墙体隔离 → Post-Collision Overlay Modifiers
    camera_modifiers: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：ParamLayer 优先级覆盖 + 类别阻断增量
      svgEl(svg, 'rect', {x: 34, y: 46, width: 126, height: 42, rx: 2, fill: 'rgba(229, 180, 67, 0.14)', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 34, y: 104, width: 126, height: 42, rx: 2, fill: 'rgba(95, 224, 138, 0.10)', stroke: JADE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 196, y: 66, width: 92, height: 60, rx: 2, fill: 'rgba(255, 251, 244, 0.06)', stroke: PALE, 'stroke-width': 1.3});
      arrow(svg, 160, 67, 194, 84, GOLD, 1.8);
      arrow(svg, 160, 125, 194, 108, JADE, 1.8);
      // 中间红色碰撞安全屏障
      svgEl(svg, 'rect', {x: 318, y: 42, width: 16, height: 146, rx: 2, fill: 'rgba(224, 101, 74, 0.22)', stroke: DANGER, 'stroke-width': 1.5});
      arrow(svg, 288, 96, 316, 96, INK, 1.8);
      arrow(svg, 334, 96, 366, 96, DANGER, 1.8);

      // 右侧：后置 Overlay 波形（欠阻尼冲量弹簧 + 黄金比三正弦噪声）
      svgEl(svg, 'rect', {x: 368, y: 44, width: 238, height: 64, rx: 2, fill: 'rgba(166, 140, 224, 0.08)', stroke: PURPLE, 'stroke-width': 1.3});
      svgEl(svg, 'path', {
        d: curvePath(t => Math.exp(-3.2 * t) * Math.sin(16 * t), 0, 1.2, 80, t => 380 + t * 176, y => 78 - y * 24),
        fill: 'none', stroke: PURPLE, 'stroke-width': 2.0,
      });
      svgEl(svg, 'rect', {x: 368, y: 120, width: 238, height: 64, rx: 2, fill: 'rgba(229, 180, 67, 0.08)', stroke: GOLD, 'stroke-width': 1.3});
      svgEl(svg, 'path', {
        d: curvePath(t => 0.6 * Math.sin(6 * t) + 0.3 * Math.sin(9.7 * t + 1.1) + 0.1 * Math.sin(15.7 * t + 2.4), 0, 2.2, 90, t => 380 + (t / 2.2) * 212, y => 156 - y * 20),
        fill: 'none', stroke: GOLD, 'stroke-width': 1.9,
      });

      addTag(host, 34, 18, 'PRE-POSE PARAMLAYER', 'mm-diag-title');
      addTag(host, 44, 58, 'Priority Overrides', 'mm-diag-gold');
      addTag(host, 44, 116, 'Blocked Deltas', 'mm-diag-jade');
      addTag(host, 206, 80, 'Param', 'mm-diag-ink');
      addTag(host, 206, 98, 'Springs', 'mm-diag-sub');
      addTag(host, 294, 192, 'Stage ④ 碰撞锁死位置', 'mm-diag-danger');
      addTag(host, 368, 18, 'POST-COLLISION OVERLAY (ROT + FOV ONLY)', 'mm-diag-title');
      addTag(host, 470, 50, 'ImpulseShake 欠阻尼', 'mm-diag-purple');
      addTag(host, 450, 126, 'NoiseSway (1 : φ : φ²)', 'mm-diag-gold');
    },

    // 11. PIP 倍镜光学色散出瞳 + 2×2 视差雅可比半楞次
    scope_assist: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：ZF39 倍镜分划 + 桶形畸变 + RGB 出瞳微偏
      const sx = 148;
      const sy = 114;
      svgEl(svg, 'circle', {cx: sx, cy: sy, r: 74, fill: 'rgba(8, 8, 10, 0.90)', stroke: 'rgba(255, 251, 244, 0.36)', 'stroke-width': 1.5});
      svgEl(svg, 'circle', {cx: sx - 3, cy: sy, r: 62, fill: 'none', stroke: 'rgba(224, 101, 74, 0.52)', 'stroke-width': 1.3});
      svgEl(svg, 'circle', {cx: sx, cy: sy, r: 62, fill: 'none', stroke: 'rgba(95, 224, 138, 0.52)', 'stroke-width': 1.3});
      svgEl(svg, 'circle', {cx: sx + 3, cy: sy, r: 62, fill: 'none', stroke: 'rgba(130, 176, 255, 0.52)', 'stroke-width': 1.3});
      // 德式 1 号 T 形分划
      svgEl(svg, 'line', {x1: sx - 58, y1: sy, x2: sx - 16, y2: sy, stroke: INK, 'stroke-width': 2.2});
      svgEl(svg, 'line', {x1: sx + 16, y1: sy, x2: sx + 58, y2: sy, stroke: INK, 'stroke-width': 2.2});
      svgEl(svg, 'path', {d: `M${sx} ${sy + 58}V${sy + 4}L${sx - 3} ${sy + 10}M${sx} ${sy + 4}L${sx + 3} ${sy + 10}`, fill: 'none', stroke: GOLD, 'stroke-width': 2.0});
      addTag(host, 34, 18, 'ZF39 4× PIP OPTICS & CHROMATIC', 'mm-diag-title');
      addTag(host, 232, 82, 'C² 边缘畸变', 'mm-diag-gold');
      addTag(host, 232, 106, 'RGB 三机制色散', 'mm-diag-sub');
      addTag(host, 232, 130, '出瞳黑边 R_p', 'mm-diag-sub');

      // 右侧：半楞次"来不拒、去则留"
      const tx = 488;
      const ty = 114;
      svgEl(svg, 'circle', {cx: tx, cy: ty, r: 68, fill: 'rgba(229, 180, 67, 0.06)', stroke: 'rgba(229, 180, 67, 0.45)', 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      svgEl(svg, 'circle', {cx: tx, cy: ty, r: 30, fill: 'rgba(229, 180, 67, 0.16)', stroke: GOLD, 'stroke-width': 1.5});
      svgEl(svg, 'circle', {cx: tx, cy: ty, r: 4, fill: INK});
      // 靠近（不阻尼）
      arrow(svg, 404, 114, 456, 114, JADE, 2.2);
      // 离开（阻尼拉回）
      arrow(svg, 520, 114, 574, 114, 'rgba(255, 251, 244, 0.35)', 1.5);
      arrow(svg, 560, 102, 526, 102, DANGER, 2.2);
      addTag(host, 382, 18, 'HALF-LENZ + J⁻¹ PARALLAX', 'mm-diag-title');
      addTag(host, 386, 132, '靠近不阻', 'mm-diag-jade');
      addTag(host, 536, 76, '远离阻尼', 'mm-diag-danger');
      addTag(host, 454, 184, 'ρ_in / ρ_out Smoothstep', 'mm-diag-sub');
    },

    // 11B. 键鼠/手柄输入响应曲线 + 精确积分转身加速斜坡
    input_curves: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：径向死区 0.20 + Linear / Power / S-Curve 曲线
      const lx = 48;
      const ly = 168;
      const lw = 236;
      const lh = 122;
      svgEl(svg, 'line', {x1: lx, y1: ly, x2: lx + lw, y2: ly, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: lx, y1: ly - lh, x2: lx, y2: ly, stroke: AXIS, 'stroke-width': 1.2});
      // 内死区阴影
      svgEl(svg, 'rect', {x: lx, y: ly - lh, width: lw * 0.2, height: lh, fill: 'rgba(224, 101, 74, 0.10)'});
      const mapX = x => lx + x * lw;
      const mapY = y => ly - y * lh;
      const dz = x => (x <= 0.2 ? 0 : (x - 0.2) / 0.8);
      // Linear
      svgEl(svg, 'path', {d: curvePath(x => dz(x), 0.2, 1, 40, mapX, mapY), fill: 'none', stroke: 'rgba(255, 251, 244, 0.34)', 'stroke-width': 1.3, 'stroke-dasharray': '4 3'});
      // Power (s=2)
      svgEl(svg, 'path', {d: curvePath(x => Math.pow(dz(x), 2), 0.2, 1, 60, mapX, mapY), fill: 'none', stroke: JADE, 'stroke-width': 1.8});
      // S-Curve (s=2.4)
      const scurve = u => {
        const a = Math.pow(u, 2.4);
        const b = Math.pow(1 - u, 2.4);
        return a + b > 0 ? a / (a + b) : u;
      };
      svgEl(svg, 'path', {d: curvePath(x => scurve(dz(x)), 0.2, 1, 70, mapX, mapY), fill: 'none', stroke: GOLD, 'stroke-width': 2.4});
      addTag(host, 44, 18, 'RADIAL DEADZONE & RESPONSE CURVES', 'mm-diag-title');
      addTag(host, 52, 134, '死区', 'mm-diag-danger');
      addTag(host, 126, 62, 'S-Curve', 'mm-diag-gold');
      addTag(host, 188, 126, 'Power', 'mm-diag-jade');
      addTag(host, 246, 174, '100%', 'mm-diag-sub');

      // 右侧：FGinkgoTurnRamp 精确积分梯形
      const rx = 368;
      const ry = 168;
      const rw = 234;
      const rh = 116;
      svgEl(svg, 'line', {x1: rx, y1: ry, x2: rx + rw, y2: ry, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: rx, y1: ry - rh, x2: rx, y2: ry, stroke: AXIS, 'stroke-width': 1.2});
      const rmx = t => rx + (t / 0.65) * rw;
      const rmy = w => ry - w * rh;
      // 帧区间 [t₀, t₁] 定积分阴影
      svgEl(svg, 'path', {
        d: `M${rmx(0.18)} ${rmy(0)}L${rmx(0.18)} ${rmy(0.267)}L${rmx(0.32)} ${rmy(0.733)}L${rmx(0.32)} ${rmy(0)}Z`,
        fill: 'rgba(166, 140, 224, 0.24)', stroke: PURPLE, 'stroke-width': 1.2,
      });
      svgEl(svg, 'path', {
        d: `M${rmx(0)} ${rmy(0)}L${rmx(0.10)} ${rmy(0)}L${rmx(0.40)} ${rmy(1)}L${rmx(0.62)} ${rmy(1)}`,
        fill: 'none', stroke: GOLD, 'stroke-width': 2.4,
      });
      addTag(host, 368, 18, 'EXACT INTEGRAL TURN RAMP (FPS-INDEP)', 'mm-diag-title');
      addTag(host, 404, 174, 'Delay 0.1s', 'mm-diag-sub');
      addTag(host, 496, 174, 'Ramp 0.3s', 'mm-diag-sub');
      addTag(host, 464, 128, 'I(t₁)-I(t₀)', 'mm-diag-purple');
      addTag(host, 534, 46, 'Full Boost', 'mm-diag-gold');
    },

    // 12. 模块化步枪挂点与 Mutable 接力 + 后坐恢复
    weapons_workbench: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 步枪轮廓
      svgEl(svg, 'path', {
        d: 'M72 98H146L158 88H304L312 98H402V106H428V114H402V120H312L300 128H248L236 168H214L220 128H172L160 156H142L152 124H118L82 138H72Z',
        fill: 'rgba(255, 251, 244, 0.08)', stroke: 'rgba(255, 251, 244, 0.80)', 'stroke-width': 1.6, 'stroke-linejoin': 'round',
      });
      // 6 模块高亮
      svgEl(svg, 'rect', {x: 192, y: 68, width: 72, height: 15, rx: 2, fill: 'rgba(229, 180, 67, 0.24)', stroke: GOLD, 'stroke-width': 1.5});
      svgEl(svg, 'rect', {x: 402, y: 100, width: 52, height: 15, rx: 1, fill: 'rgba(95, 224, 138, 0.20)', stroke: JADE, 'stroke-width': 1.5});
      svgEl(svg, 'path', {d: 'M220 132H242L234 174H212Z', fill: 'rgba(229, 180, 67, 0.22)', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 316, y: 120, width: 56, height: 14, rx: 1, fill: 'rgba(166, 140, 224, 0.20)', stroke: PURPLE, 'stroke-width': 1.3});
      // 右侧：膛内+弹匣 LIFO 栈示意
      svgEl(svg, 'rect', {x: 492, y: 50, width: 112, height: 28, rx: 2, fill: 'rgba(229, 180, 67, 0.16)', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 492, y: 92, width: 112, height: 90, rx: 2, fill: 'rgba(255, 251, 244, 0.05)', stroke: 'rgba(255, 251, 244, 0.36)', 'stroke-width': 1.2});
      [104, 122, 140, 158].forEach((ry, i) => {
        svgEl(svg, 'rect', {x: 504, y: ry, width: 88, height: 11, rx: 1, fill: i === 0 ? 'rgba(95, 224, 138, 0.36)' : 'rgba(229, 180, 67, 0.24)'});
      });
      addTag(host, 34, 18, 'M4 MODULAR WORKBENCH & LIFO FEED MECHANISM', 'mm-diag-title');
      addTag(host, 194, 44, 'Optic / Rail', 'mm-diag-gold');
      addTag(host, 386, 74, 'Muzzle Thread', 'mm-diag-jade');
      addTag(host, 254, 168, 'Mag Reload Chain', 'mm-diag-gold');
      addTag(host, 502, 56, 'Chamber (+1)', 'mm-diag-gold');
      addTag(host, 504, 184, 'Per-Round Tag Stack', 'mm-diag-sub');
    },

    // 13. 多段掩体穿射衰减 + 血量暗角曲线
    ballistics_vitals: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：两层掩体穿透衰减
      svgEl(svg, 'rect', {x: 114, y: 50, width: 22, height: 122, fill: 'rgba(255, 251, 244, 0.10)', stroke: PALE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 204, y: 50, width: 34, height: 122, fill: 'rgba(255, 251, 244, 0.12)', stroke: 'rgba(255, 251, 244, 0.65)', 'stroke-width': 1.5});
      svgEl(svg, 'line', {x1: 34, y1: 112, x2: 114, y2: 112, stroke: GOLD, 'stroke-width': 3.2});
      svgEl(svg, 'line', {x1: 136, y1: 112, x2: 204, y2: 112, stroke: PALE, 'stroke-width': 2.2});
      svgEl(svg, 'line', {x1: 238, y1: 112, x2: 312, y2: 112, stroke: DANGER, 'stroke-width': 1.4});
      svgEl(svg, 'circle', {cx: 314, cy: 112, r: 16, fill: 'rgba(224, 101, 74, 0.16)', stroke: DANGER, 'stroke-width': 1.6});
      svgEl(svg, 'path', {d: 'M307 105L321 119M321 105L307 119', stroke: INK, 'stroke-width': 1.8});
      addTag(host, 34, 18, 'PENETRATION DECAY (MAX 8 HITS)', 'mm-diag-title');
      addTag(host, 104, 176, '木 0.25', 'mm-diag-sub');
      addTag(host, 202, 176, '砖 0.50', 'mm-diag-sub');
      addTag(host, 42, 88, 'S=1.0', 'mm-diag-gold');
      addTag(host, 146, 88, 'r₁', 'mm-diag-ink');
      addTag(host, 250, 88, 'r₂', 'mm-diag-danger');

      // 右侧：血量暗角 + 喘息音量响应曲线
      svgEl(svg, 'line', {x1: 382, y1: 170, x2: 604, y2: 170, stroke: AXIS, 'stroke-width': 1.2});
      svgEl(svg, 'line', {x1: 382, y1: 44, x2: 382, y2: 170, stroke: AXIS, 'stroke-width': 1.2});
      const mx = h => 382 + (1 - h) * 214; // 左 100% HP → 右 0% HP
      const my = v => 170 - v * 116;
      svgEl(svg, 'path', {d: curvePath(m => 0.25 + 0.75 * Math.pow(m, 1.15), 0, 1, 60, m => 382 + m * 214, my), fill: 'none', stroke: DANGER, 'stroke-width': 2.2});
      const breath = m => {
        const h = 1 - m;
        const t = Math.max(0, Math.min(1, (0.60 - h) / (0.60 - 0.18)));
        const e = Math.pow(t, 0.85);
        return (3 * e * e - 2 * e * e * e) * 0.85;
      };
      svgEl(svg, 'path', {d: curvePath(breath, 0, 1, 70, m => 382 + m * 214, my), fill: 'none', stroke: GOLD, 'stroke-width': 2.0, 'stroke-dasharray': '5 3'});
      addTag(host, 382, 18, 'VIGNETTE & BREATH ENVELOPE', 'mm-diag-title');
      addTag(host, 446, 54, 'Tonemapper 暗角', 'mm-diag-danger');
      addTag(host, 480, 114, '低血喘息 (60%→18%)', 'mm-diag-gold');
      addTag(host, 382, 174, '100% HP', 'mm-diag-sub');
      addTag(host, 558, 174, '0% HP', 'mm-diag-sub');
    },

    // 14. AI 掩体 + 墙边横探球扫 + StateTree 拓扑
    ai_checkpoint: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：墙边探头球扫
      svgEl(svg, 'rect', {x: 88, y: 54, width: 108, height: 28, fill: 'rgba(255, 251, 244, 0.10)', stroke: 'rgba(255, 251, 244, 0.65)', 'stroke-width': 1.5});
      // AI 藏匿位与探出位
      svgEl(svg, 'circle', {cx: 158, cy: 112, r: 10, fill: 'rgba(255, 251, 244, 0.12)', stroke: INK, 'stroke-width': 1.4});
      svgEl(svg, 'circle', {cx: 214, cy: 112, r: 10, fill: 'rgba(229, 180, 67, 0.22)', stroke: GOLD, 'stroke-width': 1.6});
      arrow(svg, 168, 112, 204, 112, JADE, 2.0);
      // 被挡视线 vs 探出畅通视线
      svgEl(svg, 'line', {x1: 158, y1: 102, x2: 158, y2: 82, stroke: DANGER, 'stroke-width': 1.5, 'stroke-dasharray': '3 2'});
      arrow(svg, 214, 102, 260, 48, GOLD, 2.0);
      svgEl(svg, 'circle', {cx: 264, cy: 44, r: 5, fill: DANGER});
      addTag(host, 34, 18, 'WALL-PEEK EQS & r=4CM SWEEP', 'mm-diag-title');
      addTag(host, 100, 60, '高掩体墙', 'mm-diag-sub');
      addTag(host, 148, 130, 'r=4cm 横扫', 'mm-diag-jade');
      addTag(host, 224, 128, 'Peek +18cm', 'mm-diag-gold');

      // 右侧：StateTree 5 层优先决策链
      const items = [
        {y: 44, t: '1. Engage (可视交战)', c: DANGER},
        {y: 74, t: '2. Investigate (60m 枪声)', c: GOLD},
        {y: 104, t: '3. Pursue (5s 压制追击)', c: PALE},
        {y: 134, t: '4. Search (15s 搜寻)', c: PURPLE},
        {y: 164, t: '5. Patrol / StandGuard', c: JADE},
      ];
      items.forEach(it => {
        svgEl(svg, 'rect', {x: 374, y: it.y, width: 228, height: 22, rx: 2, fill: 'rgba(255, 251, 244, 0.04)', stroke: it.c, 'stroke-width': 1.2});
        addTag(host, 386, it.y + 3, it.t, 'mm-diag-ink');
      });
      addTag(host, 374, 18, 'WEAPON-DRIVEN STATETREE', 'mm-diag-title');
    },

    // 15. Weft 跨 API 零拷贝 RDG + MetaSound 音频架构
    weft_metasound: (host, svg) => {
      drawGrid(svg, 20, 16, 620, 196, 40, 36);
      // 左侧：Weft 多后端管线
      svgEl(svg, 'rect', {x: 34, y: 46, width: 124, height: 52, rx: 3, fill: 'rgba(229, 180, 67, 0.12)', stroke: GOLD, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 184, y: 46, width: 126, height: 52, rx: 3, fill: 'rgba(95, 224, 138, 0.10)', stroke: JADE, 'stroke-width': 1.4});
      arrow(svg, 158, 72, 182, 72, GOLD, 2.0);
      // 四 API 卡片
      ['D3D12', 'D3D11', 'Vulkan', 'Metal'].forEach((api, i) => {
        const bx = 34 + i * 71;
        svgEl(svg, 'rect', {x: bx, y: 128, width: 63, height: 42, rx: 2, fill: i === 0 ? 'rgba(229, 180, 67, 0.18)' : 'rgba(255, 251, 244, 0.05)', stroke: i === 0 ? GOLD : 'rgba(255, 251, 244, 0.34)', 'stroke-width': 1.2});
        addTag(host, bx + 11, 141, api, i === 0 ? 'mm-diag-gold' : 'mm-diag-sub');
      });
      arrow(svg, 246, 98, 246, 126, JADE, 1.6);
      addTag(host, 34, 18, 'WEFT MULTI-API ZERO-COPY RDG', 'mm-diag-title');
      addTag(host, 46, 54, 'QuickJS + Yoga', 'mm-diag-gold');
      addTag(host, 46, 74, 'DOM · KaTeX', 'mm-diag-sub');
      addTag(host, 196, 54, 'Immutable Packet', 'mm-diag-jade');
      addTag(host, 196, 74, 'RDG Direct Draw', 'mm-diag-sub');

      // 右侧：MetaSound 防连重 + 母线限幅器
      svgEl(svg, 'rect', {x: 364, y: 46, width: 112, height: 54, rx: 3, fill: 'rgba(166, 140, 224, 0.12)', stroke: PURPLE, 'stroke-width': 1.4});
      svgEl(svg, 'rect', {x: 500, y: 46, width: 106, height: 54, rx: 3, fill: 'rgba(229, 180, 67, 0.12)', stroke: GOLD, 'stroke-width': 1.4});
      arrow(svg, 476, 73, 498, 73, PURPLE, 2.0);
      svgEl(svg, 'rect', {x: 364, y: 124, width: 242, height: 48, rx: 3, fill: 'rgba(224, 101, 74, 0.10)', stroke: DANGER, 'stroke-width': 1.3});
      arrow(svg, 553, 100, 553, 122, GOLD, 1.6);
      addTag(host, 364, 18, 'METASOUND GRAPH & MASTER BUS', 'mm-diag-title');
      addTag(host, 374, 54, 'Random Get', 'mm-diag-purple');
      addTag(host, 374, 74, 'NoRepeats=1', 'mm-diag-sub');
      addTag(host, 510, 54, 'Pitch / dB', 'mm-diag-gold');
      addTag(host, 510, 74, '2D / 3D LPF', 'mm-diag-sub');
      addTag(host, 378, 132, 'Submix_Main Hard-Knee Limiter', 'mm-diag-danger');
      addTag(host, 378, 150, '-5.0 dB · 3ms Lookahead · 双队列命中', 'mm-diag-sub');
    },
  };

  const renderDiagram = (host, diagramId) => {
    clear(host);
    const wrap = el(host, 'div', 'mm-diag-wrap');
    const svg = svgEl(wrap, 'svg', {width: W, height: H});
    const fn = DIAGRAMS[diagramId];
    if (fn) fn(wrap, svg);
  };

  globalThis.MainMenuGalleryViews = {
    renderCodePreviewer,
    renderFormulas,
    renderStructure,
    renderDiagram,
  };
})();
