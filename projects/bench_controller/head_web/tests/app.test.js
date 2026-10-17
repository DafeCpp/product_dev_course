import { describe, expect, it } from 'vitest';

import {
  CommandTracker,
  ackText,
  cmdEStopSoft,
  cmdEnable,
  cmdMode,
  cmdSetpoint,
  cmdStop,
  fmtUptime,
  journalSeverity,
  journalText,
  lockState,
  makeIdGenerator,
  newJournalEntries,
  pushSample,
  yRange,
} from '../app.js';

const snap = (over = {}) => ({
  mode: 'displacement', holding: false, stop_reason: 'none', program_active: false,
  uptime_ms: 1000, feedback: { force_n: 10, position_mm: 2 }, setpoint: { ramped: 2 },
  bus: { mode: 'bring-up-master' }, ...over,
});

describe('commands', () => {
  it('build schema-conformant messages', () => {
    expect(cmdEnable(1, true)).toEqual({ cmd_id: 1, type: 'enable', enable: true });
    expect(cmdMode(2, 'force')).toEqual({ cmd_id: 2, type: 'set_mode', mode: 'force' });
    expect(cmdSetpoint(3, 1.5)).toEqual({ cmd_id: 3, type: 'set_setpoint', value: 1.5 });
    expect(cmdStop(4)).toEqual({ cmd_id: 4, type: 'controlled_stop' });
    expect(cmdEStopSoft(5)).toEqual({ cmd_id: 5, type: 'e_stop_soft' });
  });

  it('id generator is nonzero and monotonic', () => {
    const next = makeIdGenerator(0);
    const a = next(); const b = next();
    expect(a).toBeGreaterThan(0);
    expect(b).toBe(a + 1);
  });
});

describe('CommandTracker', () => {
  it('allows one command in flight and completes on ack', () => {
    const t = new CommandTracker();
    expect(t.submit(cmdStop(7), 0)).not.toBeNull();
    expect(t.submit(cmdStop(8), 0)).toBeNull();
    expect(t.onSnapshot({ acks: [{ cmd_id: 9 }] })).toBeNull();
    const done = t.onSnapshot({ acks: [{ cmd_id: 7, result: 'accepted' }] });
    expect(done.ack.result).toBe('accepted');
    expect(t.busy).toBe(false);
  });

  it('retries once with the same cmd_id, then times out', () => {
    const t = new CommandTracker({ timeoutMs: 100, maxRetries: 1 });
    const cmd = cmdEnable(11, true);
    t.submit(cmd, 0);
    expect(t.poll(50)).toBeNull();
    expect(t.poll(100)).toEqual({ send: cmd });
    expect(t.poll(150)).toBeNull();
    expect(t.poll(200)).toEqual({ timeout: cmd });
    expect(t.busy).toBe(false);
  });

  it('abort drops the pending command', () => {
    const t = new CommandTracker();
    t.submit(cmdStop(1), 0);
    expect(t.abort().cmd_id).toBe(1);
    expect(t.busy).toBe(false);
  });
});

describe('lockState', () => {
  it('locks everything without a connection', () => {
    expect(lockState(snap(), false).all).toBe(true);
    expect(lockState(null, true).all).toBe(true);
  });
  it('locks everything in monitor mode and during a program', () => {
    expect(lockState(snap({ bus: { mode: 'monitor' } }), true).all).toBe(true);
    expect(lockState(snap({ program_active: true }), true).all).toBe(true);
  });
  it('locks setpoint and mode while held, but not enable', () => {
    const l = lockState(snap({ holding: true, stop_reason: 'operator_controlled_stop' }), true);
    expect(l.all).toBeUndefined();
    expect(l.setpoint).toBe(true);
    expect(l.mode).toBe(true);
    const l2 = lockState(snap({ holding: true, stop_reason: 'link_safe_hold' }), true);
    expect(l2.setpoint).toBe(true);
  });
  it('is free when idle', () => {
    expect(lockState(snap(), true).reason).toBeNull();
  });
});

describe('journal', () => {
  it('returns only new entries and survives a controller restart', () => {
    const st = { lastSeq: -1, lastTotal: 0 };
    const e = (seq) => ({ seq, kind: 'command' });
    expect(newJournalEntries(st, { journal_total: 2, journal: [e(0), e(1)] })).toHaveLength(2);
    expect(newJournalEntries(st, { journal_total: 3, journal: [e(1), e(2)] })).toEqual([e(2)]);
    expect(newJournalEntries(st, { journal_total: 1, journal: [e(0)] })).toEqual([e(0)]);
  });

  it('formats and grades entries', () => {
    expect(journalSeverity({ kind: 'failure_latch' })).toBe('bad');
    expect(journalSeverity({ kind: 'hold', reason: 'operator_controlled_stop' })).toBe('warn');
    expect(journalSeverity({ kind: 'hold', reason: 'link_safe_hold' })).toBe('bad');
    expect(journalText({ kind: 'link_state', from: 'running', to: 'grace' })).toContain('→');
  });

  it('shows ack and nack with a reason', () => {
    expect(ackText({ cmd: 'enable', cmd_id: 5, result: 'accepted' })).toContain('принята');
    const nack = ackText({ cmd: 'set_setpoint', cmd_id: 6, result: 'rejected', reason: 'out_of_range' });
    expect(nack).toContain('отклонена');
    expect(ackText({ cmd: 'enable', cmd_id: 5, result: 'accepted', duplicate: true })).toContain('повтор');
  });
});

describe('chart helpers', () => {
  it('keeps a sliding window', () => {
    const h = [];
    for (let t = 0; t <= 40_000; t += 10_000) pushSample(h, snap({ uptime_ms: t }));
    expect(h[0].t).toBe(10_000);
    expect(h).toHaveLength(4);
  });

  it('pushes the unit-appropriate measurement', () => {
    const h = [];
    pushSample(h, snap({ mode: 'force' }));
    expect(h[0].meas).toBe(10);
    pushSample(h, snap({ mode: 'displacement' }));
    expect(h[1].meas).toBe(2);
  });

  it('gives a non-degenerate y range for a flat line', () => {
    const r = yRange([{ meas: 1, sp: 1 }, { meas: 1, sp: 1 }]);
    expect(r.hi ?? r[1]).toBeGreaterThan(r.lo ?? r[0]);
  });

  it('formats uptime', () => {
    expect(fmtUptime(61_000)).toBe('00:01:01');
  });
});
