// Browser side of spectra.notebook.Canvas (anywidget ESM module).
//
// Shows the latest frame the kernel rendered and sends pointer, wheel, key and
// resize events back. Events are batched with flow control: one batch is in
// flight at a time and the next goes out when the kernel acks, so a busy
// kernel never accumulates a backlog. Consecutive moves and wheel ticks are
// merged while a batch is in flight.
//
// Also: a small toolbar (Home = reset view, Save = download PNG), the data
// coordinates under the pointer, drag-to-resize height, and the notebook's
// light/dark theme reported to the kernel so the plot matches it.

// KeyboardEvent.key -> GLFW key code, for the keys the plot reacts to.
const KEYS = { r: 82, R: 82, a: 65, A: 65, g: 71, G: 71, Escape: 256 };

function mods(e) {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);
}

// True / false when the notebook's theme is known to be dark / light, null
// when it cannot be told. JupyterLab and Notebook 7 set data-jp-theme-light on
// <body>; VS Code sets vscode-dark / vscode-light classes; Colab sets
// theme="dark" on <html>. Otherwise use the background behind the output.
function notebookIsDark(el) {
  const body = document.body;
  const jp = body && body.dataset ? body.dataset.jpThemeLight : undefined;
  if (jp === "true") return false;
  if (jp === "false") return true;
  if (body && body.classList) {
    if (body.classList.contains("vscode-dark") || body.classList.contains("vscode-high-contrast"))
      return true;
    if (body.classList.contains("vscode-light")) return false;
  }
  const htmlTheme = document.documentElement.getAttribute("theme");
  if (htmlTheme === "dark") return true;
  if (htmlTheme === "light") return false;
  for (let node = el; node && node.nodeType === 1; node = node.parentElement) {
    const m = /rgba?\(([^)]+)\)/.exec(getComputedStyle(node).backgroundColor || "");
    if (!m) continue;
    const [r, g, b, a = 1] = m[1].split(",").map(Number);
    if (a === 0) continue;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b < 128;
  }
  return null;
}

function button(cls, title, text) {
  const b = document.createElement("button");
  b.className = `spectra-canvas-btn ${cls}`;
  b.type = "button";
  b.title = title;
  b.setAttribute("aria-label", title);
  b.textContent = text;
  return b;
}

function render({ model, el }) {
  const root = document.createElement("div");
  root.className = "spectra-canvas";
  root.tabIndex = 0;
  root.title = "Scroll: zoom · Drag: pan · Right-drag: axis zoom · Double-click or R: reset";
  const img = document.createElement("img");
  img.draggable = false;
  img.alt = "Spectra plot";
  const toolbar = document.createElement("div");
  toolbar.className = "spectra-canvas-toolbar";
  const home = button("spectra-canvas-home", "Reset view (R)", "⌂");
  const save = button("spectra-canvas-save", "Save as PNG", "⤓");
  toolbar.append(home, save);
  const readout = document.createElement("div");
  readout.className = "spectra-canvas-readout";
  root.append(img, toolbar, readout);
  el.appendChild(root);

  // ── Frames ────────────────────────────────────────────────────────────
  let url = null;
  function showFrame() {
    const data = model.get("frame");
    if (!data || data.byteLength === 0) return;
    const next = URL.createObjectURL(new Blob([data], { type: model.get("frame_mime") }));
    const prev = url;
    url = next;
    img.onload = () => {
      if (prev) URL.revokeObjectURL(prev);
    };
    img.src = next;
  }
  function showReadout() {
    const text = model.get("readout") || "";
    readout.textContent = text;
    readout.style.display = text ? "" : "none";
  }
  function applyDark() {
    root.classList.toggle("spectra-dark", !!model.get("dark"));
  }

  // ── Event batching with flow control ─────────────────────────────────
  let pending = [];
  let inflight = false;
  let watchdog = null;

  function flush() {
    if (inflight || pending.length === 0) return;
    inflight = true;
    model.send({ type: "events", events: pending });
    pending = [];
    // If an ack is ever lost (kernel restart), do not stall input forever.
    clearTimeout(watchdog);
    watchdog = setTimeout(() => {
      inflight = false;
      flush();
    }, 2000);
  }

  function push(ev) {
    const last = pending[pending.length - 1];
    if (last && last.t === ev.t && (ev.t === "move" || ev.t === "resize" || ev.t === "leave")) {
      pending[pending.length - 1] = ev;
    } else if (last && ev.t === "wheel" && last.t === "wheel" && last.mode === ev.mode) {
      last.dx += ev.dx;
      last.dy += ev.dy;
      last.x = ev.x;
      last.y = ev.y;
    } else {
      pending.push(ev);
    }
    flush();
  }

  function download(buffer, name) {
    const href = URL.createObjectURL(new Blob([buffer], { type: "image/png" }));
    const a = document.createElement("a");
    a.href = href;
    a.download = name || "spectra.png";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(href), 1000);
  }

  function onMsg(msg, buffers) {
    if (!msg) return;
    if (msg.type === "ack") {
      inflight = false;
      clearTimeout(watchdog);
      flush();
    } else if (msg.type === "png" && buffers && buffers.length) {
      download(buffers[0], msg.name);
    }
  }

  function pos(e) {
    const r = img.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  // ── Pointer, wheel and keyboard ──────────────────────────────────────
  function onPointerDown(e) {
    root.focus({ preventScroll: true });
    img.setPointerCapture(e.pointerId);
    push({ t: "down", b: e.button, m: mods(e), ...pos(e) });
    e.preventDefault();
  }
  function onPointerUp(e) {
    if (img.hasPointerCapture(e.pointerId)) img.releasePointerCapture(e.pointerId);
    push({ t: "up", b: e.button, m: mods(e), ...pos(e) });
  }
  function onPointerMove(e) {
    push({ t: "move", ...pos(e) });
  }
  function onPointerLeave() {
    push({ t: "leave" });
  }
  function onWheel(e) {
    e.preventDefault(); // zoom the plot, not the notebook
    push({ t: "wheel", dx: e.deltaX, dy: e.deltaY, mode: e.deltaMode, ...pos(e) });
  }
  function key(code, m) {
    push({ t: "key", k: code, a: 1, m });
    push({ t: "key", k: code, a: 0, m });
  }
  function onKeyDown(e) {
    const code = KEYS[e.key];
    if (code === undefined || e.ctrlKey || e.metaKey) return;
    key(code, mods(e));
    e.preventDefault();
    e.stopPropagation(); // keep Jupyter's command-mode shortcuts out of it
  }
  const onHome = (e) => {
    e.stopPropagation();
    key(KEYS.r, 0);
  };
  const onSave = (e) => {
    e.stopPropagation();
    model.send({ type: "save" });
  };
  const onDblClick = () => key(KEYS.r, 0);
  const noMenu = (e) => e.preventDefault();

  img.addEventListener("pointerdown", onPointerDown);
  img.addEventListener("pointerup", onPointerUp);
  img.addEventListener("pointercancel", onPointerUp);
  img.addEventListener("pointermove", onPointerMove);
  img.addEventListener("pointerleave", onPointerLeave);
  img.addEventListener("wheel", onWheel, { passive: false });
  img.addEventListener("dblclick", onDblClick);
  img.addEventListener("contextmenu", noMenu);
  root.addEventListener("keydown", onKeyDown);
  home.addEventListener("click", onHome);
  save.addEventListener("click", onSave);

  // ── Size: follow the output width; the user may drag the height ──────
  // The kernel renders at devicePixelRatio x this CSS size and the <img>
  // shows it at CSS size, so the plot stays sharp on HiDPI screens.
  let resizeTimer = null;
  function sendSize() {
    const w = root.clientWidth;
    const h = root.clientHeight || model.get("height");
    if (w > 0 && h > 0) push({ t: "resize", w, h, dpr: window.devicePixelRatio || 1 });
  }
  function applyHeight() {
    root.style.height = `${model.get("height")}px`;
    sendSize();
  }
  function onResized() {
    // Dragging the resize handle changes the height: keep the model in step
    // so the new size survives a re-display.
    const h = Math.round(root.clientHeight);
    if (h > 0 && h !== model.get("height")) {
      model.set("height", h);
      model.save_changes();
    }
    sendSize();
  }
  const observer = new ResizeObserver(() => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(onResized, 100);
  });
  observer.observe(root);

  // devicePixelRatio changes with browser zoom or when the window moves to
  // another screen: render again at the new ratio.
  let dprQuery = null;
  function onDprChange() {
    watchDpr();
    sendSize();
  }
  function watchDpr() {
    if (dprQuery) dprQuery.removeEventListener("change", onDprChange);
    dprQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
    dprQuery.addEventListener("change", onDprChange);
  }
  watchDpr();

  // ── Theme: tell the kernel whether the notebook is dark ─────────────
  let lastDark = null;
  function sendTheme() {
    const dark = notebookIsDark(el);
    if (dark === null || dark === lastDark) return;
    lastDark = dark;
    model.send({ type: "theme", dark });
  }
  const themeObserver = new MutationObserver(sendTheme);
  themeObserver.observe(document.body, {
    attributes: true,
    attributeFilter: ["data-jp-theme-light", "class"],
  });
  // Styles apply once the output is attached: check on the next frame.
  const themeFrame = requestAnimationFrame(sendTheme);

  model.on("change:frame", showFrame);
  model.on("change:height", applyHeight);
  model.on("change:readout", showReadout);
  model.on("change:dark", applyDark);
  model.on("msg:custom", onMsg);
  applyHeight();
  showFrame();
  showReadout();
  applyDark();

  return () => {
    observer.disconnect();
    themeObserver.disconnect();
    cancelAnimationFrame(themeFrame);
    if (dprQuery) dprQuery.removeEventListener("change", onDprChange);
    clearTimeout(resizeTimer);
    clearTimeout(watchdog);
    model.off("change:frame", showFrame);
    model.off("change:height", applyHeight);
    model.off("change:readout", showReadout);
    model.off("change:dark", applyDark);
    model.off("msg:custom", onMsg);
    if (url) URL.revokeObjectURL(url);
  };
}

export default { render };
