// Страница отладки bench-head (LOS-187). Vanilla JS без сборки и внешних
// зависимостей: статику прошивка встраивает и отдаёт по офлайн softAP.
// Контракт сообщений: docs/experiment-control/bench-command-schema.md.
//
// Файл — ES-модуль: чистая логика экспортируется для vitest (без DOM,
// таймеров и сокетов), UI поднимается только в браузере.

export const WINDOW_MS = 30_000;
export const ACK_TIMEOUT_MS = 1000;
export const MAX_RETRIES = 1;

// ───────────────────────── Форматирование ─────────────────────────

export function fmtNum(v, digits = 2) {
  return typeof v === 'number' && Number.isFinite(v) ? v.toFixed(digits) : '—';
}

export function fmtUptime(ms) {
  if (typeof ms !== 'number' || !Number.isFinite(ms)) return '--:--:--';
  const s = Math.floor(ms / 1000);
  const p = (n) => String(n).padStart(2, '0');
  return `${p(Math.floor(s / 3600))}:${p(Math.floor((s % 3600) / 60))}:${p(s % 60)}`;
}

export const unitFor = (mode) => (mode === 'force' ? 'Н' : 'мм');

const TEXT = {
  link: { running: 'связь есть', grace: 'grace: ждём связь', ramp_down: 'плавная разгрузка', safe_hold: 'безопасное удержание' },
  stop: {
    none: 'нет',
    operator_controlled_stop: 'оператор: controlled stop',
    operator_e_stop_soft: 'оператор: stop (soft)',
    link_safe_hold: 'потеря связи',
    specimen_failure: 'разрушение образца',
  },
  reason: {
    none: '', invalid_id: 'неверный cmd_id', program_running: 'идёт программа',
    held: 'защитное удержание', stopped: 'остановлено: сначала enable',
    out_of_range: 'значение вне лимитов',
  },
  cmd: {
    enable: 'enable', set_mode: 'режим', set_setpoint: 'уставка',
    controlled_stop: 'controlled stop', e_stop_soft: 'stop (soft)',
  },
  chain: { unknown: 'нет данных', ok: 'замкнута', open: 'РАЗОМКНУТА' },
  busMode: { bring_up_master: 'bring-up-мастер', monitor: 'монитор (только слушаем)' },
};
export const linkText = (s) => TEXT.link[s] ?? s ?? '—';
export const stopText = (s) => TEXT.stop[s] ?? s ?? '—';
export const chainText = (s) => TEXT.chain[s] ?? s ?? '—';
export const busModeText = (s) => TEXT.busMode[s] ?? s ?? '—';

export function ackText(ack) {
  const name = TEXT.cmd[ack.cmd] ?? ack.cmd;
  const dup = ack.duplicate ? ' (повтор, не исполнялась)' : '';
  if (ack.result === 'accepted') return `${name} #${ack.cmd_id}: принята${dup}`;
  return `${name} #${ack.cmd_id}: отклонена — ${TEXT.reason[ack.reason] ?? ack.reason}${dup}`;
}

// ───────────────────────── Команды ─────────────────────────

export function makeIdGenerator(seed = Math.floor(Date.now() / 1000)) {
  // 0 зарезервирован; id в диапазоне uint32
  let n = (seed % 2_000_000_000) + 1;
  return () => n++;
}

export const cmdEnable = (id, enable) => ({ cmd_id: id, type: 'enable', enable });
export const cmdMode = (id, mode) => ({ cmd_id: id, type: 'set_mode', mode });
export const cmdSetpoint = (id, value) => ({ cmd_id: id, type: 'set_setpoint', value });
export const cmdStop = (id) => ({ cmd_id: id, type: 'controlled_stop' });
export const cmdEStopSoft = (id) => ({ cmd_id: id, type: 'e_stop_soft' });

/**
 * Одна команда «в полёте» (очередь прошивки single-slot): следующую нельзя
 * слать, пока нет ack. Нет ack за timeout — повтор с тем же cmd_id (прошивка
 * идемпотентна), после MAX_RETRIES — сообщаем об отсутствии ответа.
 */
export class CommandTracker {
  constructor({ timeoutMs = ACK_TIMEOUT_MS, maxRetries = MAX_RETRIES } = {}) {
    this.timeoutMs = timeoutMs;
    this.maxRetries = maxRetries;
    this.pending = null;
  }

  get busy() { return this.pending !== null; }

  /** @returns {object|null} команда для отправки или null, если занято */
  submit(cmd, nowMs) {
    if (this.pending) return null;
    this.pending = { cmd, sentAt: nowMs, retries: 0 };
    return cmd;
  }

  /** @returns {{cmd, ack}|null} завершённая команда */
  onSnapshot(snap) {
    if (!this.pending) return null;
    const ack = (snap.acks ?? []).find((a) => a.cmd_id === this.pending.cmd.cmd_id);
    if (!ack) return null;
    const done = { cmd: this.pending.cmd, ack };
    this.pending = null;
    return done;
  }

  /** @returns {{send: object}|{timeout: object}|null} */
  poll(nowMs) {
    const p = this.pending;
    if (!p || nowMs - p.sentAt < this.timeoutMs) return null;
    if (p.retries < this.maxRetries) {
      p.retries += 1;
      p.sentAt = nowMs;
      return { send: p.cmd };
    }
    this.pending = null;
    return { timeout: p.cmd };
  }

  /** Соединение потеряно: ack уже не придёт */
  abort() {
    const cmd = this.pending?.cmd ?? null;
    this.pending = null;
    return cmd;
  }
}

// ───────────────────────── Блокировки формы (S2) ─────────────────────────

/**
 * Что запрещено в форме ручного ввода. Причины показываются оператору;
 * прошивка всё равно перепроверяет и ответит nack.
 */
export function lockState(snap, connected) {
  if (!connected || !snap) return { all: true, reason: 'нет соединения с контроллером' };
  if (snap.bus?.mode === 'monitor') {
    return { all: true, reason: 'режим монитора: голова только слушает, ручной ввод запрещён' };
  }
  if (snap.program_active) {
    return { all: true, reason: 'идёт программа нагружения: ручной ввод заблокирован (остановите её)' };
  }
  const operatorStop = snap.holding && String(snap.stop_reason).startsWith('operator');
  if (operatorStop) {
    return { setpoint: true, mode: true, reason: 'операторский останов: уставка и режим заблокированы, для продолжения нажмите «Применить enable» с галкой' };
  }
  if (snap.holding) {
    return { setpoint: true, mode: true, reason: `защитное удержание (${stopText(snap.stop_reason)}): ручной ввод заблокирован, сбрасывается вне этого интерфейса` };
  }
  return { reason: null };
}

// ───────────────────────── Журнал (S5) ─────────────────────────

export function journalText(e) {
  switch (e.kind) {
    case 'command': return `${ackText(e)}`;
    case 'link_state': return `связь: ${linkText(e.from)} → ${linkText(e.to)}`;
    case 'failure_latch': return 'латч детектора: разрушение образца';
    case 'hold': return `удержание позиции: ${stopText(e.reason)}`;
    default: return `${e.kind}`;
  }
}

export function journalSeverity(e) {
  if (e.kind === 'failure_latch') return 'bad';
  if (e.kind === 'hold') return String(e.reason).startsWith('operator') ? 'warn' : 'bad';
  if (e.kind === 'link_state') return e.to === 'running' ? '' : 'warn';
  if (e.kind === 'command' && e.result === 'rejected') return 'warn';
  return '';
}

/** Новые записи (seq больше виденного); перезапуск контроллера сбрасывает счётчик */
export function newJournalEntries(state, snap) {
  const total = snap.journal_total ?? 0;
  if (total < state.lastTotal) state.lastSeq = -1;
  state.lastTotal = total;
  const fresh = (snap.journal ?? []).filter((e) => e.seq > state.lastSeq);
  for (const e of fresh) state.lastSeq = Math.max(state.lastSeq, e.seq);
  return fresh;
}

// ───────────────────────── График (S3) ─────────────────────────

export function pushSample(history, snap, windowMs = WINDOW_MS) {
  const fb = snap.feedback ?? {};
  history.push({
    t: snap.uptime_ms,
    mode: snap.mode,
    meas: snap.mode === 'force' ? fb.force_n : fb.position_mm,
    sp: snap.setpoint?.ramped,
  });
  const cutoff = snap.uptime_ms - windowMs;
  while (history.length && history[0].t < cutoff) history.shift();
  return history;
}

/** Диапазон оси Y с запасом; при ровной линии — минимальная высота */
export function yRange(samples) {
  let lo = Infinity, hi = -Infinity;
  for (const s of samples) {
    for (const v of [s.meas, s.sp]) {
      if (Number.isFinite(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    }
  }
  if (!Number.isFinite(lo)) return { lo: -1, hi: 1 };
  const span = Math.max(hi - lo, Math.abs(hi) * 0.1, 1e-6);
  return { lo: lo - span * 0.1, hi: hi + span * 0.1 };
}

export function drawChart(canvas, history, colors) {
  const ctx = canvas.getContext('2d');
  const { width: w, height: h } = canvas;
  ctx.clearRect(0, 0, w, h);
  const mode = history.length ? history[history.length - 1].mode : 'displacement';
  const samples = history.filter((s) => s.mode === mode);
  if (samples.length < 2) return;
  const { lo, hi } = yRange(samples);
  const t1 = samples[samples.length - 1].t;
  const t0 = t1 - WINDOW_MS;
  const x = (t) => ((t - t0) / WINDOW_MS) * w;
  const y = (v) => h - 20 - ((v - lo) / (hi - lo)) * (h - 40);

  ctx.strokeStyle = colors.grid; ctx.fillStyle = colors.text; ctx.font = '12px sans-serif';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const v = lo + ((hi - lo) * i) / 4;
    ctx.beginPath(); ctx.moveTo(0, y(v)); ctx.lineTo(w, y(v)); ctx.stroke();
    ctx.fillText(`${fmtNum(v, Math.abs(hi) > 100 ? 0 : 2)} ${unitFor(mode)}`, 4, y(v) - 3);
  }
  const line = (key, color) => {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    let started = false;
    for (const s of samples) {
      if (!Number.isFinite(s[key])) { started = false; continue; }
      if (!started) { ctx.moveTo(x(s.t), y(s[key])); started = true; } else ctx.lineTo(x(s.t), y(s[key]));
    }
    ctx.stroke();
  };
  line('sp', colors.sp);
  line('meas', colors.meas);
}

// ───────────────────────── UI (только в браузере) ─────────────────────────

function initUi() {
  const $ = (id) => document.getElementById(id);
  const css = getComputedStyle(document.documentElement);
  const colors = {
    grid: css.getPropertyValue('--bord').trim() || '#2d323c',
    text: css.getPropertyValue('--txt2').trim() || '#8b8f9a',
    meas: css.getPropertyValue('--accent').trim() || '#5b8af5',
    sp: css.getPropertyValue('--warn').trim() || '#ff9f0a',
  };
  const state = {
    ws: null, connected: false, snap: null, reconnectMs: 500,
    tracker: new CommandTracker(), nextId: makeIdGenerator(),
    history: [], journal: { lastSeq: -1, lastTotal: 0 }, resultEl: null,
    formTouched: false,
  };

  const setBadge = (el, text, cls) => { el.textContent = text; el.className = `badge ${cls}`; };
  const setResult = (el, text, cls) => { el.textContent = text; el.className = `result ${cls}`; };

  // ---- отправка ----
  function send(cmd) { state.ws?.send(JSON.stringify(cmd)); }

  function submit(cmd, resultEl) {
    if (!state.connected) return setResult(resultEl, 'нет соединения', 'bad');
    const out = state.tracker.submit(cmd, performance.now());
    if (!out) return setResult(resultEl, 'дождитесь ответа на предыдущую команду', 'pending');
    state.resultEl = resultEl;
    setResult(resultEl, `${TEXT.cmd[cmd.type]} #${cmd.cmd_id}: ожидаем подтверждение…`, 'pending');
    send(out);
    renderLocks();
  }

  // ---- рендер ----
  function renderLocks() {
    const lock = lockState(state.snap, state.connected);
    const busy = state.tracker.busy;
    const banner = $('s2-lock');
    banner.hidden = !lock.reason;
    banner.textContent = lock.reason ?? '';
    $('s2-enable').disabled = !!lock.all;
    $('s2-enable-send').disabled = !!lock.all || busy;
    $('s2-mode').disabled = !!(lock.all || lock.mode);
    $('s2-mode-send').disabled = !!(lock.all || lock.mode) || busy;
    $('s2-value').disabled = !!(lock.all || lock.setpoint);
    $('s2-value-send').disabled = !!(lock.all || lock.setpoint) || busy;
    const off = !state.connected || busy;
    $('s4-stop').disabled = off;
    $('s4-estop').disabled = off;
  }

  function renderBus(s) {
    const bus = s.bus;
    $('s1-nodata').hidden = !bus || bus.frames_seen !== false;
    $('s1-busmode').textContent = bus ? busModeText(bus.mode) : 'нет данных';
    setBadge($('b-bus'), `шина: ${bus ? busModeText(bus.mode) : '—'}`,
      !bus ? 'unknown' : bus.frames_seen === false ? 'bad' : bus.mode === 'monitor' ? 'warn' : 'ok');
    const tbody = $('s1-nodes');
    tbody.replaceChildren();
    const nodes = bus?.nodes ?? [];
    if (!nodes.length) {
      const tr = tbody.insertRow(); const td = tr.insertCell(); td.colSpan = 4; td.className = 'muted'; td.textContent = 'нет данных';
      return;
    }
    for (const n of nodes) {
      const tr = tbody.insertRow();
      tr.insertCell().textContent = `#${n.id}`;
      tr.insertCell().textContent = n.nmt ?? '—';
      tr.insertCell().textContent = n.hb_age_ms == null ? '—' : `${n.hb_age_ms} мс`;
      const c = tr.insertCell(); c.textContent = n.hb_fresh ? 'свежий' : 'устарел'; c.className = n.hb_fresh ? '' : 'muted';
    }
  }

  function renderSnapshot(s) {
    state.snap = s;
    const fb = s.feedback ?? {};
    setBadge($('b-link'), `связь: ${linkText(s.link_state)}`, s.link_state === 'running' ? 'ok' : s.link_state === 'safe_hold' ? 'bad' : 'warn');
    $('b-uptime').textContent = `⏱ ${fmtUptime(s.uptime_ms)}`;
    renderBus(s);

    // S2: подставляем состояние, пока оператор не правит форму
    const unit = unitFor(s.mode);
    $('s2-unit').textContent = unit;
    if (document.activeElement !== $('s2-mode')) $('s2-mode').value = s.mode;
    if (document.activeElement !== $('s2-enable')) $('s2-enable').checked = !!s.enabled;
    $('s2-sp').textContent = `${fmtNum(s.setpoint?.commanded)} / ${fmtNum(s.setpoint?.ramped)} ${unit}`;

    // S3
    $('s3-force').textContent = fmtNum(fb.force_n, 1);
    $('s3-pos').textContent = fmtNum(fb.position_mm, 3);
    $('s3-status').textContent = fb.status == null ? '—' : `0x${Number(fb.status).toString(16).toUpperCase().padStart(2, '0')}`;
    setBadge($('s3-fresh'), fb.fresh ? 'fresh' : 'stale', fb.fresh ? 'ok' : 'bad');
    $('s3-age').textContent = `age ${fb.age_ticks ?? '—'} тиков`;
    $('s3-valve').textContent = fmtNum(s.valve_command, 3);
    $('s3-leg-meas').textContent = s.mode === 'force' ? 'усилие' : 'перемещение';
    pushSample(state.history, s);
    drawChart($('s3-chart'), state.history, colors);

    // S4
    $('s4-link').textContent = linkText(s.link_state);
    $('s4-hold').textContent = s.holding ? `да (${stopText(s.stop_reason)})` : 'нет';
    $('s4-failure').textContent = s.failure_latched ? 'ЗАЩЁЛКНУТ' : 'нет';
    $('s4-chain').textContent = chainText(s.estop_chain);

    // S5
    $('s5-fw').textContent = s.info?.fw ?? 'нет данных';
    $('s5-cfg').textContent = s.info?.config ?? 'нет данных';
    $('s5-up').textContent = fmtUptime(s.uptime_ms);
    const t = s.tick_us ?? {};
    $('s5-tick').textContent = t.count ? `${t.min} / ${t.avg} / ${t.p99} / ${t.max}` : '—';
    $('s5-stop').textContent = stopText(s.stop_reason);
    const c = s.counters ?? {};
    $('s5-cnt').textContent = `${c.cmd_total ?? 0} / ${c.cmd_rejected ?? 0} / ${c.cmd_duplicate ?? 0} / ${c.cmd_overwritten ?? 0}`;
    renderJournal(s);

    // ack
    const done = state.tracker.onSnapshot(s);
    if (done && state.resultEl) {
      setResult(state.resultEl, ackText(done.ack), done.ack.result === 'accepted' ? 'ok' : 'bad');
    }
    renderLocks();
  }

  function renderJournal(s) {
    const fresh = newJournalEntries(state.journal, s);
    if (!fresh.length) return;
    const ol = $('s5-log');
    if (ol.firstElementChild?.classList.contains('muted')) ol.replaceChildren();
    for (const e of fresh) {
      const li = document.createElement('li');
      const sev = journalSeverity(e);
      if (sev) li.className = sev;
      const t = document.createElement('span'); t.className = 't'; t.textContent = fmtUptime(e.t_ms);
      li.append(t, journalText(e));
      ol.prepend(li);
    }
    while (ol.children.length > 200) ol.lastElementChild.remove();
  }

  // ---- соединение ----
  function connect() {
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(`${scheme}://${location.host}/ws`);
    state.ws = ws;
    ws.onopen = () => { state.connected = true; state.reconnectMs = 500; setBadge($('b-ws'), 'WS', 'ok'); renderLocks(); };
    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch { return; }
      if (msg.type === 'snapshot') renderSnapshot(msg);
      else if (msg.type === 'error' && state.tracker.busy && state.resultEl) {
        state.tracker.abort();
        setResult(state.resultEl, `команда не принята прошивкой: ${msg.error}`, 'bad');
        renderLocks();
      }
    };
    ws.onclose = () => {
      state.connected = false; setBadge($('b-ws'), 'WS', 'off');
      const lost = state.tracker.abort();
      if (lost && state.resultEl) setResult(state.resultEl, 'соединение потеряно, результат неизвестен', 'bad');
      renderLocks();
      setTimeout(connect, state.reconnectMs);
      state.reconnectMs = Math.min(state.reconnectMs * 2, 5000);
    };
  }

  // ---- обработчики ----
  $('s2-enable-send').onclick = () => submit(cmdEnable(state.nextId(), $('s2-enable').checked), $('s2-result'));
  $('s2-mode-send').onclick = () => submit(cmdMode(state.nextId(), $('s2-mode').value), $('s2-result'));
  $('s2-value-send').onclick = () => {
    const v = parseFloat($('s2-value').value);
    if (!Number.isFinite(v)) return setResult($('s2-result'), 'введите число', 'bad');
    submit(cmdSetpoint(state.nextId(), v), $('s2-result'));
  };
  $('s2-form').onsubmit = (e) => e.preventDefault();
  $('s4-stop').onclick = () => submit(cmdStop(state.nextId()), $('s4-result'));
  $('s4-estop').onclick = () => submit(cmdEStopSoft(state.nextId()), $('s4-result'));

  setInterval(() => {
    const r = state.tracker.poll(performance.now());
    if (!r) return;
    if (r.send) { send(r.send); return; }
    if (r.timeout && state.resultEl) {
      setResult(state.resultEl, `${TEXT.cmd[r.timeout.type]} #${r.timeout.cmd_id}: нет ответа от контроллера`, 'bad');
      renderLocks();
    }
  }, 250);

  renderLocks();
  connect();
}

if (typeof document !== 'undefined' && document.getElementById('app-root')) {
  initUi();
}
