// 仅展示 C++ 给出的地点和透明度；暂停、排队、存档结果与完成时刻由本地控制器决定。
(() => {
  const root = document.getElementById('root');
  const title = document.createElement('div');
  title.className = 'location-title';
  root.appendChild(title);
  weft.engine.on('location-title', event => {
    const data = typeof event === 'string' ? JSON.parse(event) : event;
    const name = data.uppercase ? String(data.title || '').toUpperCase() : String(data.title || '');
    title.textContent = name;
    title.style.opacity = String(data.opacity || 0);
    const family = String(data.fontFamily || 'sans-serif').replace(/["\\]/g, '');
    title.style.fontFamily = `"${family}", sans-serif`;
    title.style.fontWeight = String(Math.min(900, Math.max(100, data.fontWeight || 600)));
    title.style.color = data.color || '#f6f3eb';
    // 长地点名缩字，保留屏幕两侧留白；大字不因自动折行变成遮住半个视野的段落。
    const characters = Array.from(name);
    const count = characters.reduce((sum, c) => sum + (c.charCodeAt(0) > 255 ? 1 : .6), 0);
    const spacing = Math.min(1, Math.max(0, data.letterSpacing || 0));
    const widthFraction = Math.min(1, Math.max(.1, data.maxWidthFraction || .86));
    const fontSize = Math.min(Math.max(1, data.fontSize || 200), (data.width || 1920) * widthFraction / Math.max(count + characters.length * spacing, 1));
    title.style.fontSize = `${fontSize}px`;
    title.style.letterSpacing = `${fontSize * spacing}px`;
  });
})();
