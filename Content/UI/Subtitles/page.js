(() => {
  const root = document.getElementById('root');
  root.className = 'gk-subtitle-hud';
  const stack = document.createElement('div');
  stack.className = 'gk-caption-stack';
  root.appendChild(stack);
  const rows = [GinkgoCaption.create(stack), GinkgoCaption.create(stack)];
  weft.engine.on('subtitles', message => {
    const data = typeof message === 'string' ? JSON.parse(message) : message;
    rows.forEach((update, index) => update(data.lines[index], data));
  });
})();
