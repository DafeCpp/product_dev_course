import React, { useEffect, useMemo, useRef, useState } from 'react'

type Scenario = 'steady' | 'bursts' | 'dropout' | 'out_of_order' | 'late_data'

type Waveform = 'sine' | 'pulses' | 'saw'

type TelemetryIngestReading = {
    timestamp: string
    raw_value: number
    physical_value?: number | null
    meta?: Record<string, unknown>
}

type TelemetryIngestBody = {
    sensor_id: string
    run_id?: string | null
    capture_session_id?: string | null
    meta?: Record<string, unknown>
    readings: TelemetryIngestReading[]
}

type StreamEvent =
    | { kind: 'telemetry'; data: Record<string, unknown> }
    | { kind: 'error'; data: string }
    | { kind: 'heartbeat' }

const TELEMETRY_BASE = '/telemetry'

function mulberry32(seed: number): () => number {
    let t = seed >>> 0
    return () => {
        t += 0x6d2b79f5
        let r = Math.imul(t ^ (t >>> 15), 1 | t)
        r ^= r + Math.imul(r ^ (r >>> 7), 61 | r)
        return ((r ^ (r >>> 14)) >>> 0) / 4294967296
    }
}

function clamp(n: number, min: number, max: number): number {
    return Math.max(min, Math.min(max, n))
}

function posMod(n: number, mod: number): number {
    // works for negative n as well
    return ((n % mod) + mod) % mod
}

function waveformValue(waveform: Waveform, tSec: number, amplitude: number, periodSec: number, dutyCycle: number): number {
    const a = Math.abs(amplitude)
    const p = Math.max(0.001, periodSec)
    const phase = posMod(tSec, p) / p // 0..1

    if (waveform === 'sine') return Math.sin(2 * Math.PI * phase) * a
    if (waveform === 'saw') return (2 * phase - 1) * a // -A..+A ramp

    // pulses: 0..A rectangular impulses
    const d = clamp(dutyCycle, 0, 1)
    return phase < d ? a : 0
}

function safeJsonParse(value: string): unknown {
    try {
        return JSON.parse(value)
    } catch {
        return null
    }
}

function nowIso(): string {
    return new Date().toISOString()
}

function uuidLike(value: string): boolean {
    return /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(value.trim())
}

async function readTextOrJson(resp: Response): Promise<string> {
    const ct = resp.headers.get('content-type') || ''
    const text = await resp.text()
    if (ct.includes('application/json')) {
        const parsed = safeJsonParse(text)
        return parsed ? JSON.stringify(parsed) : text
    }
    return text
}

async function postTelemetry(body: TelemetryIngestBody, token: string): Promise<{ ok: boolean; status: number; text: string }> {
    const resp = await fetch(`${TELEMETRY_BASE}/api/v1/telemetry`, {
        method: 'POST',
        headers: {
            Authorization: `Bearer ${token}`,
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(body),
    })
    return { ok: resp.ok, status: resp.status, text: await readTextOrJson(resp) }
}

async function* sseFetchStream(
    url: string,
    headers: Record<string, string>,
    signal?: AbortSignal
): AsyncGenerator<StreamEvent, void, void> {
    const resp = await fetch(url, { method: 'GET', headers, signal })
    if (!resp.ok || !resp.body) {
        const text = await readTextOrJson(resp)
        yield { kind: 'error', data: `HTTP ${resp.status}: ${text}` }
        return
    }

    const reader = resp.body.getReader()
    const decoder = new TextDecoder()
    let buffer = ''

    while (true) {
        if (signal?.aborted) return
        const { done, value } = await reader.read()
        if (done) break
        buffer += decoder.decode(value, { stream: true }).replace(/\r\n/g, '\n')

        // SSE events separated by blank line
        let idx: number
        // eslint-disable-next-line no-cond-assign
        while ((idx = buffer.indexOf('\n\n')) >= 0) {
            const chunk = buffer.slice(0, idx)
            buffer = buffer.slice(idx + 2)

            const lines = chunk.split('\n').map((l) => l.trimEnd())
            if (lines.length === 1 && lines[0].startsWith(':')) {
                yield { kind: 'heartbeat' }
                continue
            }

            let eventName = ''
            let dataLines: string[] = []
            for (const line of lines) {
                if (!line) continue
                if (line.startsWith('event:')) eventName = line.slice('event:'.length).trim()
                else if (line.startsWith('data:')) dataLines.push(line.slice('data:'.length).trimStart())
            }
            const dataStr = dataLines.join('\n')
            if (eventName === 'telemetry') {
                const parsed = safeJsonParse(dataStr)
                yield { kind: 'telemetry', data: (parsed as Record<string, unknown>) || { raw: dataStr } }
            } else if (eventName === 'error') {
                yield { kind: 'error', data: dataStr || 'Unknown stream error' }
            }
        }
    }
}

export function App() {
    const [sensorId, setSensorId] = useState('')
    const [sensorToken, setSensorToken] = useState('')
    const [runId, setRunId] = useState('')
    const [captureSessionId, setCaptureSessionId] = useState('')

    const [scenario, setScenario] = useState<Scenario>('steady')
    const [rateHz, setRateHz] = useState(10)
    const [batchSize, setBatchSize] = useState(50)
    const [seed, setSeed] = useState(42)
    const [burstEverySec, setBurstEverySec] = useState(12)
    const [burstDurationSec, setBurstDurationSec] = useState(3)
    const [dropoutEverySec, setDropoutEverySec] = useState(18)
    const [dropoutDurationSec, setDropoutDurationSec] = useState(6)
    const [lateSeconds, setLateSeconds] = useState(3600)
    const [outOfOrderFraction, setOutOfOrderFraction] = useState(0.2)

    const [waveform, setWaveform] = useState<Waveform>('sine')
    const [amplitude, setAmplitude] = useState(10)
    const [periodSec, setPeriodSec] = useState(5)
    const [dutyCycle, setDutyCycle] = useState(0.1)

    const [isRunning, setIsRunning] = useState(false)
    const [sent, setSent] = useState(0)
    const [accepted, setAccepted] = useState(0)
    const [errors, setErrors] = useState(0)
    const [lastHttpStatus, setLastHttpStatus] = useState<number | null>(null)
    const [log, setLog] = useState<string>('')

    const tickRef = useRef<number | null>(null) // setTimeout id
    const seqRef = useRef(0)
    const lastTimestampRef = useRef<number>(Date.now())
    const rng = useMemo(() => mulberry32(seed), [seed])

    const [streamOn, setStreamOn] = useState(false)
    const streamAbortRef = useRef<AbortController | null>(null)
    const [streamSinceId, setStreamSinceId] = useState(0)
    const [streamEvents, setStreamEvents] = useState<Record<string, unknown>[]>([])
    const [streamStatus, setStreamStatus] = useState<'idle' | 'connected' | 'error'>('idle')

    function appendLog(line: string) {
        setLog((prev) => {
            const next = `${prev}${prev ? '\n' : ''}${line}`
            // keep last ~300 lines
            const lines = next.split('\n')
            if (lines.length <= 300) return next
            return lines.slice(lines.length - 300).join('\n')
        })
    }

    const canSend = useMemo(() => uuidLike(sensorId) && sensorToken.trim().length > 0, [sensorId, sensorToken])

    function buildReadings(n: number, effectiveRateHz: number): TelemetryIngestReading[] {
        const readings: TelemetryIngestReading[] = []

        const now = Date.now()
        const stepMs = 1000 / clamp(effectiveRateHz, 1, 10_000)

        // internal monotonic timestamp for steady generation
        let base = Math.max(lastTimestampRef.current, now)
        if (!isRunning) base = now

        for (let i = 0; i < n; i++) {
            const tMs = base + i * stepMs
            const t = tMs / 1000
            const noise = (rng() - 0.5) * 0.15
            const raw =
                waveformValue(waveform, t, clamp(amplitude, 0, 1_000_000), clamp(periodSec, 0.001, 1_000_000), dutyCycle) +
                noise * 10 +
                20
            const phys = raw * 1.0

            let ts = new Date(tMs).toISOString()
            if (scenario === 'late_data') ts = new Date(tMs - lateSeconds * 1000).toISOString()

            readings.push({
                timestamp: ts,
                raw_value: raw,
                physical_value: phys,
                meta: {
                    seq: seqRef.current++,
                    scenario,
                    generated_at: nowIso(),
                },
            })
        }

        lastTimestampRef.current = base + (n - 1) * stepMs

        if (scenario === 'out_of_order') {
            const frac = clamp(outOfOrderFraction, 0, 1)
            const m = Math.floor(readings.length * frac)
            for (let i = 0; i < m; i++) {
                const a = Math.floor(rng() * readings.length)
                const b = Math.floor(rng() * readings.length)
                const tmp = readings[a]
                readings[a] = readings[b]
                readings[b] = tmp
            }
        }

        return readings
    }

    async function sendBatch(n: number, effectiveRateHz: number) {
        const body: TelemetryIngestBody = {
            sensor_id: sensorId.trim(),
            run_id: runId.trim() || null,
            capture_session_id: captureSessionId.trim() || null,
            meta: {
                source: 'sensor-simulator-web',
                scenario,
                rate_hz: effectiveRateHz,
                batch_size: n,
                signal: {
                    waveform,
                    amplitude: clamp(amplitude, 0, 1_000_000),
                    period_sec: clamp(periodSec, 0.001, 1_000_000),
                    duty_cycle: waveform === 'pulses' ? clamp(dutyCycle, 0, 1) : null,
                },
            },
            readings: buildReadings(n, effectiveRateHz),
        }

        appendLog(`[${nowIso()}] POST /api/v1/telemetry readings=${n}`)
        const t0 = performance.now()
        try {
            const res = await postTelemetry(body, sensorToken.trim())
            const dt = Math.round(performance.now() - t0)
            setLastHttpStatus(res.status)
            if (res.ok) {
                setSent((v) => v + n)
                const parsed = safeJsonParse(res.text) as { accepted?: unknown } | null
                const acc = parsed && typeof parsed.accepted === 'number' ? parsed.accepted : n
                setAccepted((v) => v + acc)
                appendLog(`[${nowIso()}] ✅ ${res.status} in ${dt}ms: ${res.text}`)
            } else {
                setErrors((v) => v + 1)
                appendLog(`[${nowIso()}] ❌ ${res.status} in ${dt}ms: ${res.text}`)
            }
        } catch (e: any) {
            setErrors((v) => v + 1)
            appendLog(`[${nowIso()}] ❌ network error: ${String(e?.message || e)}`)
        }
    }

    function scenarioIsPausedAt(nowSec: number): boolean {
        if (scenario === 'dropout') {
            const cycle = dropoutEverySec + dropoutDurationSec
            if (cycle <= 0) return false
            const phase = nowSec % cycle
            return phase >= dropoutEverySec
        }
        return false
    }

    function scenarioEffectiveRate(nowSec: number): number {
        if (scenario === 'bursts') {
            const cycle = burstEverySec + burstDurationSec
            if (cycle <= 0) return rateHz
            const phase = nowSec % cycle
            if (phase >= burstEverySec) return clamp(rateHz * 8, 1, 10_000)
        }
        return rateHz
    }

    function start() {
        if (!canSend) return
        setIsRunning(true)
        appendLog(`[${nowIso()}] ▶️ start (${scenario})`)

        const startMs = Date.now()
        const scheduleNext = () => {
            const elapsedSec = Math.floor((Date.now() - startMs) / 1000)
            const effRate = scenarioEffectiveRate(elapsedSec)
            const effBatch = clamp(batchSize, 1, 10_000)
            const intervalMs = clamp(Math.round((1000 * effBatch) / clamp(effRate, 1, 10_000)), 50, 60_000)
            tickRef.current = window.setTimeout(async () => {
                if (scenarioIsPausedAt(elapsedSec)) {
                    appendLog(`[${nowIso()}] ⏸️ dropout window`)
                } else {
                    await sendBatch(effBatch, effRate)
                }
                if (tickRef.current !== null) scheduleNext()
            }, intervalMs)
        }

        // kick off immediately
        tickRef.current = window.setTimeout(() => {
            scheduleNext()
        }, 0)
    }

    function stop() {
        setIsRunning(false)
        if (tickRef.current) {
            window.clearTimeout(tickRef.current)
            tickRef.current = null
        }
        appendLog(`[${nowIso()}] ⏹ stop`)
    }

    useEffect(() => {
        return () => {
            if (tickRef.current) window.clearTimeout(tickRef.current)
            if (streamAbortRef.current) streamAbortRef.current.abort()
        }
    }, [])

    async function connectStream() {
        if (!uuidLike(sensorId) || !sensorToken.trim()) return
        setStreamOn(true)
        setStreamStatus('idle')
        appendLog(`[${nowIso()}] 🔌 stream connect since_id=${streamSinceId}`)

        const controller = new AbortController()
        streamAbortRef.current = controller

        const url = new URL(`${TELEMETRY_BASE}/api/v1/telemetry/stream`, window.location.origin)
        url.searchParams.set('sensor_id', sensorId.trim())
        if (streamSinceId > 0) url.searchParams.set('since_id', String(streamSinceId))

        try {
            setStreamStatus('connected')
            for await (const ev of sseFetchStream(
                url.toString(),
                {
                    Authorization: `Bearer ${sensorToken.trim()}`,
                },
                controller.signal
            )) {
                if (controller.signal.aborted) break
                if (ev.kind === 'heartbeat') continue
                if (ev.kind === 'error') {
                    setStreamStatus('error')
                    appendLog(`[${nowIso()}] ⚠️ stream error: ${ev.data}`)
                    continue
                }
                setStreamEvents((prev) => {
                    const next = [...prev, ev.data]
                    return next.length > 200 ? next.slice(next.length - 200) : next
                })
                const maybeId = (ev.data as any)?.id
                if (typeof maybeId === 'number') setStreamSinceId(maybeId)
            }
        } catch (e: any) {
            setStreamStatus('error')
            appendLog(`[${nowIso()}] ⚠️ stream exception: ${String(e?.message || e)}`)
        } finally {
            setStreamOn(false)
        }
    }

    function disconnectStream() {
        setStreamOn(false)
        streamAbortRef.current?.abort()
        streamAbortRef.current = null
        appendLog(`[${nowIso()}] 🔌 stream disconnect`)
    }

    const pillClass =
        lastHttpStatus === null ? 'pill' : lastHttpStatus >= 200 && lastHttpStatus < 300 ? 'pill ok' : 'pill bad'

    return (
        <div className="container">
            <div className="header">
                <div className="title">
                    <h1>Sensor Simulator</h1>
                    <p>
                        Симулирует телеметрию и отправляет батчи в <span className="badge">/telemetry → telemetry-ingest-service</span>.
                        Для ingest нужен <span className="badge">Authorization: Bearer &lt;sensor token&gt;</span>.
                    </p>
                </div>
                <div className={pillClass} title="Last ingest HTTP status">
                    <span className="dot" />
                    <span>HTTP {lastHttpStatus ?? '—'}</span>
                </div>
            </div>

            <div className="grid">
                <div className="card">
                    <h2>Ingest (POST /api/v1/telemetry)</h2>

                    <div className="row">
                        <div>
                            <label>sensor_id</label>
                            <input
                                value={sensorId}
                                onChange={(e) => setSensorId(e.target.value)}
                                placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                            />
                            <div className="hint">UUID датчика из experiment-service.</div>
                        </div>
                        <div>
                            <label>sensor token (Bearer)</label>
                            <input value={sensorToken} onChange={(e) => setSensorToken(e.target.value)} placeholder="token..." />
                            <div className="hint">Токен датчика (возвращается при регистрации/rotate-token).</div>
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>run_id (optional)</label>
                            <input value={runId} onChange={(e) => setRunId(e.target.value)} placeholder="uuid..." />
                        </div>
                        <div>
                            <label>capture_session_id (optional)</label>
                            <input
                                value={captureSessionId}
                                onChange={(e) => setCaptureSessionId(e.target.value)}
                                placeholder="uuid..."
                            />
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>scenario</label>
                            <select value={scenario} onChange={(e) => setScenario(e.target.value as Scenario)}>
                                <option value="steady">steady stream</option>
                                <option value="bursts">bursts</option>
                                <option value="dropout">dropout</option>
                                <option value="out_of_order">out-of-order</option>
                                <option value="late_data">late data</option>
                            </select>
                            <div className="hint">Меняет тайминги/порядок/таймстемпы генерируемых readings.</div>
                        </div>
                        <div>
                            <label>seed (deterministic)</label>
                            <input
                                type="number"
                                value={seed}
                                onChange={(e) => setSeed(Number(e.target.value))}
                                placeholder="42"
                            />
                            <div className="hint">Один и тот же seed → одинаковый шум/перемешивание.</div>
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>signal waveform</label>
                            <select value={waveform} onChange={(e) => setWaveform(e.target.value as Waveform)}>
                                <option value="sine">sine</option>
                                <option value="pulses">rect pulses</option>
                                <option value="saw">saw</option>
                            </select>
                            <div className="hint">Форма сигнала для raw_value (плюс шум и смещение +20).</div>
                        </div>
                        <div>
                            <label>amplitude</label>
                            <input
                                type="number"
                                value={amplitude}
                                min={0}
                                step={1}
                                onChange={(e) => setAmplitude(Number(e.target.value))}
                            />
                            <div className="hint">Для sine/saw: размах; для pulses: высота импульса.</div>
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>period (sec)</label>
                            <input
                                type="number"
                                value={periodSec}
                                min={0.001}
                                step={0.1}
                                onChange={(e) => setPeriodSec(Number(e.target.value))}
                            />
                            <div className="hint">Один период сигнала в секундах.</div>
                        </div>
                        <div>
                            <label>duty cycle (0..1)</label>
                            <input
                                type="number"
                                value={dutyCycle}
                                min={0}
                                max={1}
                                step={0.05}
                                disabled={waveform !== 'pulses'}
                                onChange={(e) => setDutyCycle(Number(e.target.value))}
                            />
                            <div className="hint">Только для pulses: доля периода, когда импульс “вкл”.</div>
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>base rate (Hz)</label>
                            <input
                                type="number"
                                value={rateHz}
                                min={1}
                                max={10000}
                                onChange={(e) => setRateHz(Number(e.target.value))}
                            />
                        </div>
                        <div>
                            <label>batch size</label>
                            <input
                                type="number"
                                value={batchSize}
                                min={1}
                                max={10000}
                                onChange={(e) => setBatchSize(Number(e.target.value))}
                            />
                            <div className="hint">В ingest лимит: максимум 10k readings на запрос.</div>
                        </div>
                    </div>

                    {(scenario === 'bursts' || scenario === 'dropout' || scenario === 'late_data' || scenario === 'out_of_order') && (
                        <div className="row" style={{ marginTop: 10 }}>
                            {scenario === 'bursts' && (
                                <>
                                    <div>
                                        <label>burst every (sec)</label>
                                        <input
                                            type="number"
                                            value={burstEverySec}
                                            min={1}
                                            onChange={(e) => setBurstEverySec(Number(e.target.value))}
                                        />
                                    </div>
                                    <div>
                                        <label>burst duration (sec)</label>
                                        <input
                                            type="number"
                                            value={burstDurationSec}
                                            min={1}
                                            onChange={(e) => setBurstDurationSec(Number(e.target.value))}
                                        />
                                    </div>
                                </>
                            )}
                            {scenario === 'dropout' && (
                                <>
                                    <div>
                                        <label>dropout every (sec)</label>
                                        <input
                                            type="number"
                                            value={dropoutEverySec}
                                            min={1}
                                            onChange={(e) => setDropoutEverySec(Number(e.target.value))}
                                        />
                                    </div>
                                    <div>
                                        <label>dropout duration (sec)</label>
                                        <input
                                            type="number"
                                            value={dropoutDurationSec}
                                            min={1}
                                            onChange={(e) => setDropoutDurationSec(Number(e.target.value))}
                                        />
                                    </div>
                                </>
                            )}
                            {scenario === 'late_data' && (
                                <div>
                                    <label>late seconds (timestamp - N)</label>
                                    <input
                                        type="number"
                                        value={lateSeconds}
                                        min={1}
                                        onChange={(e) => setLateSeconds(Number(e.target.value))}
                                    />
                                </div>
                            )}
                            {scenario === 'out_of_order' && (
                                <div>
                                    <label>out-of-order fraction (0..1)</label>
                                    <input
                                        type="number"
                                        value={outOfOrderFraction}
                                        min={0}
                                        max={1}
                                        step={0.05}
                                        onChange={(e) => setOutOfOrderFraction(Number(e.target.value))}
                                    />
                                </div>
                            )}
                        </div>
                    )}

                    <div className="actions">
                        <button className="btn primary" disabled={!canSend || isRunning} onClick={() => start()}>
                            Start stream
                        </button>
                        <button className="btn danger" disabled={!isRunning} onClick={() => stop()}>
                            Stop
                        </button>
                        <button
                            className="btn"
                            disabled={!canSend || isRunning}
                            onClick={() => void sendBatch(clamp(batchSize, 1, 10_000), clamp(rateHz, 1, 10_000))}
                        >
                            Send one batch
                        </button>
                        <button
                            className="btn"
                            onClick={() => {
                                setSent(0)
                                setAccepted(0)
                                setErrors(0)
                                setLog('')
                                setStreamEvents([])
                                appendLog(`[${nowIso()}] 🧹 reset counters/log`)
                            }}
                        >
                            Reset
                        </button>
                    </div>

                    <div className="kpis">
                        <div className="kpi">
                            <div className="label">generated readings</div>
                            <div className="value">{sent}</div>
                        </div>
                        <div className="kpi">
                            <div className="label">accepted (from API)</div>
                            <div className="value">{accepted}</div>
                        </div>
                        <div className="kpi">
                            <div className="label">errors</div>
                            <div className="value">{errors}</div>
                        </div>
                        <div className="kpi">
                            <div className="label">state</div>
                            <div className="value">{isRunning ? 'running' : 'idle'}</div>
                        </div>
                    </div>

                    <div className="log">{log || 'log is empty'}</div>
                </div>

                <div className="card">
                    <h2>SSE stream (GET /api/v1/telemetry/stream)</h2>
                    <div className="row">
                        <div>
                            <label>since_id</label>
                            <input
                                type="number"
                                value={streamSinceId}
                                min={0}
                                onChange={(e) => setStreamSinceId(Number(e.target.value))}
                            />
                            <div className="hint">Последний увиденный telemetry_records.id.</div>
                        </div>
                        <div>
                            <label>status</label>
                            <input value={streamStatus} readOnly />
                            <div className="hint">Connected/idle/error.</div>
                        </div>
                    </div>

                    <div className="actions">
                        <button className="btn good" disabled={!canSend || streamOn} onClick={() => void connectStream()}>
                            Connect
                        </button>
                        <button className="btn danger" disabled={!streamOn} onClick={() => disconnectStream()}>
                            Disconnect
                        </button>
                        <button className="btn" onClick={() => setStreamEvents([])}>
                            Clear events
                        </button>
                    </div>

                    <div className="hint" style={{ marginTop: 10 }}>
                        Stream использует <span className="badge">fetch + ReadableStream</span>, т.к. EventSource не умеет кастомные
                        заголовки (нужен Authorization).
                    </div>

                    <textarea
                        readOnly
                        value={
                            streamEvents.length
                                ? streamEvents
                                    .slice(-50)
                                    .map((e) => JSON.stringify(e))
                                    .join('\n')
                                : ''
                        }
                        placeholder="events will appear here..."
                        style={{ marginTop: 10 }}
                    />
                </div>
            </div>
        </div>
    )
}

