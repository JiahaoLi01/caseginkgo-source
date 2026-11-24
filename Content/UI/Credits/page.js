// C++ 是唯一时钟：进入时生成随机顺序，帧事件只传采样权重；隐藏期间不跑独立 CSS 动画。
(() => {
  const root = document.getElementById('root');
  const block = document.createElement('div');
  block.className = 'credit';
  root.appendChild(block);
  let rows = [], glyphs = [], settings = {}, width = 0, height = 0;
  let lastTime = -1, lastOpacity = -1;
  const clamp = (value, low, high, fallback) => Number.isFinite(value) ? Math.min(high, Math.max(low, value)) : fallback;
  const decode = data => typeof data === 'string' ? JSON.parse(data) : data;

  function addRow(italic) {
    const node = document.createElement('div');
    node.className = 'credit-line';
    node.style.fontStyle = italic ? 'italic' : 'normal';
    block.appendChild(node);
    rows.push({ node, units: 0 });
  }

  function fitRows() {
    const availableWidth = width * settings.width;
    // 多人共用字号，按最长一行适配宽度。行数很多时再一起缩小，使最上方职务仍在视口内。
    const longest = rows.slice(1).reduce((units, row) => Math.max(units, row.units), 1);
    const nameSize = Math.min(settings.name, availableWidth / longest);
    const roleSize = Math.min(settings.role, availableWidth / Math.max(1, rows[0].units));
    const totalHeight = (roleSize + (rows.length - 1) * nameSize) * 1.2 + settings.gap;
    const scale = Math.min(1, height * (1 - settings.bottom) / totalHeight);
    rows.forEach(({ node }, index) => {
      const size = (index === 0 ? roleSize : nameSize) * scale;
      node.style.fontSize = `${size}px`;
      node.style.lineHeight = `${size * 1.2}px`;
      node.style.minHeight = `${size * 1.2}px`;
      node.style.letterSpacing = `${size * settings.spacing}px`;
      if (index === 1) node.style.marginTop = `${settings.gap * scale}px`;
    });
  }

  weft.engine.on('credit', event => {
    const data = decode(event);
    block.textContent = '';
    rows = [];
    addRow(false);
    block.style.opacity = '0';
    lastOpacity = 0;
    lastTime = -1;
    block.style.fontFamily = `"${String(data.font || 'sans-serif').replace(/["\\]/g, '')}", sans-serif`;
    block.style.fontWeight = String(clamp(data.weight, 100, 900, 600));
    const color = data.color || '#f6f3eb';
    block.style.color = color;
    const left = clamp(data.left, 0, .8, .085);
    block.style.left = `${left * 100}%`;
    const bottom = clamp(data.bottom, 0, .8, .18);
    block.style.bottom = `${bottom * 100}%`;
    settings = {
      role: clamp(data.roleSize, 1, 256, 20), name: clamp(data.nameSize, 1, 256, 44),
      spacing: clamp(data.spacing, 0, 1, .1), reveal: Math.max(0, data.reveal),
      width: Math.min(clamp(data.width, .1, .8, .28), 1 - left),
      bottom, gap: clamp(data.gap, 0, 100, 5)
    };
    // Weft 将 text-shadow 的字形覆盖率按半径缓存。CSS 阴影长度为高斯标准差的两倍；
    // 采用离散半径以复用遮罩，避免逐帧为每个字符做元素离屏滤镜。
    const blur = clamp(data.blur, 0, 32, 10);
    const bands = Math.ceil(blur);
    settings.shadows = Array.from({ length: bands + 1 }, (_, i) =>
      i === 0 ? 'none' : `0px 0px ${2 * blur * i / bands}px ${color}`);
    glyphs = (data.glyphs || []).map(g => {
      const node = document.createElement('span');
      node.className = 'credit-glyph';
      const blank = /^\s+$/.test(g.text);
      // Weft 的独立空白 flex item 没有固有宽度。空格用留白盒占位，避免零宽文本使原生绘制拒绝整帧。
      node.textContent = blank ? '' : g.text;
      if (blank) node.style.width = `${(.25 + settings.spacing) * g.text.length}em`;
      if (g.row === rows.length) addRow(g.italic);
      const row = rows[g.row];
      row.node.appendChild(node);
      row.units += (g.text.charCodeAt(0) > 255 ? 1 : .65) + settings.spacing;
      return { node, blank, delay: g.delay, alpha: -1, band: -1 };
    });
    width = 0;
  });

  weft.engine.on('credit-frame', event => {
    const data = decode(event);
    if (lastOpacity !== data.opacity) {
      lastOpacity = data.opacity;
      block.style.opacity = String(clamp(lastOpacity, 0, 1, 0));
    }
    if (glyphs.length && data.viewportWidth > 0 && data.viewportHeight > 0 &&
        (width !== data.viewportWidth || height !== data.viewportHeight)) {
      width = data.viewportWidth;
      height = data.viewportHeight;
      fitRows();
    }
    if (lastTime === data.time || !glyphs.length) return;
    lastTime = data.time;
    glyphs.forEach(g => {
      if (g.blank || g.alpha === 1) return;
      const local = lastTime - g.delay;
      if (local < 0) return;
      const t = settings.reveal === 0 ? 1 : Math.min(1, local / settings.reveal);
      const progress = t * t * (3 - 2 * t);
      const alpha = Math.round(progress * 255) / 255;
      if (alpha !== g.alpha) {
        g.node.style.opacity = String(alpha);
        g.alpha = alpha;
      }
      const band = Math.round((1 - progress) * (settings.shadows.length - 1));
      if (band !== g.band) {
        g.node.style.color = band ? 'transparent' : 'inherit';
        g.node.style.textShadow = settings.shadows[band];
        g.band = band;
      }
    });
  });
})();
