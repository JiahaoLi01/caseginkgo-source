// 游戏与设置页使用同一行组件。文字使用 textContent，台词中的标点、换行和尖括号均原样显示。
(() => {
  function create(parent) {
    const row = document.createElement('div');
    row.className = 'gk-caption';
    const glow = document.createElement('div');
    glow.className = 'gk-caption-glow';
    const text = document.createElement('div');
    text.className = 'gk-caption-text';
    row.appendChild(glow);
    row.appendChild(text);
    parent.appendChild(row);
    return (line, style) => {
      row.style.display = line ? 'flex' : 'none';
      if (!line) return;
      text.textContent = style.speaker && line.name ? line.name + '：' + line.text : line.text;
      text.style.fontSize = style.size + 'px';
      glow.style.opacity = String(style.backdrop);
      row.style.transitionDuration = (line.fading ? style.fadeMs : 0) + 'ms';
      row.style.opacity = line.fading ? '0' : '1';
    };
  }
  globalThis.GinkgoCaption = { create };
})();
