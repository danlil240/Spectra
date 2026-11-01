// Browser side of spectra.notebook.Canvas (anywidget ESM module).
//
// Shows the latest frame the kernel rendered and sends pointer, wheel, key and
// resize events back. Events are batched with flow control: one batch is in
// flight at a time and the next goes out when the kernel acks, so a busy
// kernel never accumulates a backlog. Consecutive moves and wheel ticks are
// merged while a batch is in flight.

// KeyboardEvent.key -> GLFW key code, for the keys the plot reacts to.
const KEYS = { r: 82, R: 82, a: 65, A: 65, g: 71, G: 71, Escape: 256 };

function mods(e) {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);
}

function render({ model, el }) {
  const root = document.createElement("div");
  root.className = "spectra-canvas";
  root.tabIndex = 0;
  root.title = "Scroll: zoom · Drag: pan · Right-drag: axis zoom · Double-click or R: reset";
  const img = document.createElement("img");
  img.draggable = false;
  img.alt = "Spectra plot";
  const reset = document.createElement("button");
  reset.className = "spectra-canvas-reset";
  reset.type = "button";
  reset.title = "Reset view (R)";
  reset.textContent = "↺";
  root.append(img, reset);
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
    if (last && last.t === ev.t && (ev.t === "move" || ev.t === "resize")) {
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

  function onMsg(msg) {
    if (msg && msg.type === "ack") {
      inflight = false;
      clearTimeout(watchdog);
      flush();
    }
  }

  function pos(e) {
    const r = img.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  // ── Pointer, wheel and keyboard ──────────────────────────────────────
  function onPointerDown(e) {
    if (e.target === reset) return;
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
  const onReset = (e) => {
    e.stopPropagation();
    key(KEYS.r, 0);
  };
  const onDblClick = () => key(KEYS.r, 0);
  const noMenu = (e) => e.preventDefault();

  img.addEventListener("pointerdown", onPointerDown);
  img.addEventListener("pointerup", onPointerUp);
  img.addEventListener("pointercancel", onPointerUp);
  img.addEventListener("pointermove", onPointerMove);
  img.addEventListener("wheel", onWheel, { passive: false });
  img.addEventListener("dblclick", onDblClick);
  img.addEventListener("contextmenu", noMenu);
  root.addEventListener("keydown", onKeyDown);
  reset.addEventListener("click", onReset);

  // ── Size: follow the output width at the device pixel ratio ─────────
  let resizeTimer = null;
  function sendSize() {
    const w = root.clientWidth;
    const h = model.get("height");
    if (w > 0 && h > 0) push({ t: "resize", w, h, dpr: window.devicePixelRatio || 1 });
  }
  function applyHeight() {
    root.style.height = `${model.get("height")}px`;
    sendSize();
  }
  const observer = new ResizeObserver(() => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(sendSize, 100);
  });
  observer.observe(root);

  model.on("change:frame", showFrame);
  model.on("change:height", applyHeight);
  model.on("msg:custom", onMsg);
  applyHeight();
  showFrame();

  return () => {
    observer.disconnect();
    clearTimeout(resizeTimer);
    clearTimeout(watchdog);
    model.off("change:frame", showFrame);
    model.off("change:height", applyHeight);
    model.off("msg:custom", onMsg);
    if (url) URL.revokeObjectURL(url);
  };
}

export default { render };
