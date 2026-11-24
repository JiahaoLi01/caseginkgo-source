// 换肩射线的专用编辑器。只管理一份值快照，通过 onChange 整组提交；保存、撤销与运行时校验仍由调参桥接负责。
// 长度在界面显示为米，消息保持 Unreal 的 cm；图为相机坐标的平视俯视示意，不另发物理查询。
(() => {
  const {C, el, clear, fmt, clamp, svgEl, svgRoot} = Paper;
  const copy = rays => rays.map(ray => ({angle: ray.angle, length: ray.length, score: ray.score}));
  const side = angle => angle < 0 ? '左' : angle > 0 ? '右' : '正前方';

  /** {value, maxItems, onChange(rays)}；set 接收引擎回传值，相同列表不重建，正在编辑的输入框保留焦点。 */
  const create = (parent, opts) => {
    const root = el(parent, 'div', 'ct-ray-editor');
    const toolbar = el(root, 'div', 'ct-ray-toolbar');
    const count = el(toolbar, 'div', 'ct-ray-count');
    const add = Paper.button(toolbar, '＋ 添加射线', false, () => {
      if (rays.length >= opts.maxItems) return;
      selected = rays.length;
      commit([...rays, {angle: 0, length: 350, score: 1}]);
    });
    const figure = el(root, 'div', 'ct-ray-figure');
    const caption = el(root, 'div', 'ct-ray-caption');
    const headings = el(root, 'div', 'ct-ray-row ct-ray-head');
    for (const [cls, text] of [['index', '射线'], ['side', '计分侧'], ['value', '角度 °'], ['value', '长度 m'], ['value', '命中分'], ['remove', '']]) {
      el(headings, 'div', `ct-ray-${cls}`, text);
    }
    const list = el(root, 'div', 'ct-ray-list');
    el(root, 'div', 'ct-ray-hint', '两肩共用此布局：负角度向左，正角度向右，0° 向前。回车或移开焦点应用。');

    /** 当前引擎值的副本，提交时再复制，避免回传之前污染调参页的比较基准。 */
    let rays = [];

    /** 选中条目的列表下标；只影响编辑器高亮，删掉条目时夹到剩余列表内。 */
    let selected = 0;

    /** 每条射线对应的输入控件；仅在增删时重建，修改数值只更新原节点。 */
    let rows = [];

    /** 最近一次绘制的值，避免引擎确认同一列表时重复操作 DOM。 */
    let shown = '';

    const pick = index => {
      selected = index;
      updateRows();
      draw();
    };

    const draw = () => {
      clear(figure);
      // 无 viewBox 的 Weft SVG 使用固定像素坐标；完整圆面支持负向/后向射线，统一缩放保证长度可比较。
      const width = 480, height = 232, x = width / 2, y = height / 2, radius = 94;
      const extent = Math.max(100, ...rays.map(ray => ray.length));
      const svg = svgRoot(figure, width, height);
      for (const r of [radius / 2, radius]) svgEl(svg, 'circle', {cx: x, cy: y, r, fill: 'none', stroke: 'rgba(255,251,244,0.16)', 'stroke-width': 1});
      svgEl(svg, 'line', {x1: x, y1: y - radius, x2: x, y2: y + radius, stroke: 'rgba(255,251,244,0.14)'});
      svgEl(svg, 'line', {x1: x - radius, y1: y, x2: x + radius, y2: y, stroke: 'rgba(255,251,244,0.14)'});
      for (const index of rays.map((_, i) => i).sort((a, b) => (a === selected) - (b === selected))) {
        const ray = rays[index], radians = ray.angle * Math.PI / 180;
        const dx = Math.sin(radians) * ray.length / extent * radius, dy = -Math.cos(radians) * ray.length / extent * radius;
        const color = index === selected ? C.ginkgo : ray.angle < 0 ? 'rgba(95,224,138,0.72)' : ray.angle > 0 ? 'rgba(229,154,74,0.72)' : 'rgba(255,251,244,0.72)';
        svgEl(svg, 'line', {x1: x, y1: y, x2: x + dx, y2: y + dy, stroke: color, 'stroke-width': index === selected ? 3 : 1.5});
        svgEl(svg, 'circle', {cx: x + dx, cy: y + dy, r: index === selected ? 5 : 3, fill: color});
      }
      svgEl(svg, 'circle', {cx: x, cy: y, r: 4, fill: C.ink1});
      for (const [text, left, top] of [['前', x - 7, 0], ['后', x - 7, height - 18], ['左', x - radius - 28, y - 9], ['右', x + radius + 14, y - 9], [`外圈 ${fmt(extent / 100)} m`, 12, height - 24]]) {
        const label = el(figure, 'div', 'ct-ray-axis', text);
        label.style.left = `${left}px`;
        label.style.top = `${top}px`;
      }
      // 用图面上的距离选线，避开 SVG 子节点命中行为差异；重叠线可在列表中逐条选中。
      svg.addEventListener('click', event => {
        const rect = svg.getBoundingClientRect(), px = event.clientX - rect.left - x, py = event.clientY - rect.top - y;
        let best = -1, distance = 12;
        rays.forEach((ray, index) => {
          const angle = ray.angle * Math.PI / 180, dx = Math.sin(angle) * ray.length / extent * radius, dy = -Math.cos(angle) * ray.length / extent * radius;
          const t = clamp((px * dx + py * dy) / Math.max(1e-9, dx * dx + dy * dy), 0, 1);
          const d = Math.hypot(px - t * dx, py - t * dy);
          if (d < distance) { best = index; distance = d; }
        });
        if (best >= 0) pick(best);
      });
      const ray = rays[selected];
      caption.textContent = ray ? `#${selected + 1} · ${side(ray.angle)} · ${fmt(ray.angle)}° · ${fmt(ray.length / 100)} m · 命中 +${fmt(ray.score)}` : '还没有射线 · 添加后开始检测';
    };

    const commit = value => {
      set(value);
      opts.onChange(copy(rays));
    };

    const edit = (index, key, value) => {
      const next = copy(rays);
      next[index][key] = value;
      selected = index;
      commit(next);
    };

    const rebuild = () => {
      clear(list);
      rows = rays.map((ray, index) => {
        const row = el(list, 'div', 'ct-ray-row');
        const number = el(row, 'div', 'ct-ray-index', String(index + 1));
        number.addEventListener('click', () => pick(index));
        const which = el(row, 'div', 'ct-ray-side');
        which.addEventListener('click', () => pick(index));
        const fields = {};
        for (const [key, scale, min, max] of [['angle', 1, -180, 180], ['length', 100, 0.01, 100], ['score', 1, 0, 1000]]) {
          const cell = el(row, 'div', 'ct-ray-value');
          fields[key] = Paper.numberField(cell, {
            value: ray[key] / scale, format: v => fmt(v, 3),
            onCommit: v => {
              const bounded = Math.round(clamp(v, min, max) * 1000) / 1000;
              fields[key].root.value = fmt(bounded, 3);
              edit(index, key, bounded * scale);
            },
          });
          fields[key].root.addEventListener('focus', () => pick(index));
          // Paper.numberField 对非法文本不提交；离焦时恢复权威值，避免无效文本看起来已经生效。
          fields[key].root.addEventListener('blur', () => { if (rays[index]) fields[key].root.value = fmt(rays[index][key] / scale, 3); });
        }
        const remove = el(row, 'div', 'ct-ray-remove');
        Paper.button(remove, '×', false, () => {
          if (selected > index) --selected;
          commit(rays.filter((_, i) => i !== index));
        });
        return {root: row, side: which, fields};
      });
    };

    const updateRows = () => rows.forEach((row, index) => {
      row.root.className = `ct-ray-row${index === selected ? ' ct-ray-selected' : ''}`;
      row.side.textContent = side(rays[index].angle);
      for (const key of ['angle', 'length', 'score']) row.fields[key].set(rays[index][key] / (key === 'length' ? 100 : 1));
    });

    const set = value => {
      const next = Array.isArray(value) ? value : [];
      const text = JSON.stringify(next);
      if (text === shown) return;
      shown = text;
      rays = copy(next);
      selected = clamp(selected, 0, Math.max(0, rays.length - 1));
      if (rows.length !== rays.length) rebuild();
      count.textContent = `每肩 ${rays.length} / ${opts.maxItems} 条`;
      add.style.opacity = rays.length >= opts.maxItems ? '0.4' : '1';
      updateRows();
      draw();
    };
    set(opts.value);
    return {root, set};
  };
  globalThis.CameraRayEditor = {create};
})();
