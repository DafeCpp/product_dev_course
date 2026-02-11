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

type PersistedSettings = {
    scenario: Scenario
    rateHz: number
    batchSize: number
    seed: number
    burstEverySec: number
    burstDurationSec: number
    dropoutEverySec: number
    dropoutDurationSec: number
    lateSeconds: number
    outOfOrderFraction: number
    waveform: Waveform
    amplitude: number
    periodSec: number
    dutyCycle: number
}

type SensorConfig = {
    key: string
    label: string
    sensorId: string
    sensorToken: string
    runId: string
    captureSessionId: string
    streamSinceId: number
    settings: PersistedSettings
}

type PersistedStateV2 = {
    version: 2
    sensors: SensorConfig[]
    selectedSensorKey: string
}

const STORAGE_KEY = 'sensor-simulator:params:v1'

const DEFAULT_SETTINGS: PersistedSettings = {
    scenario: 'steady',
    rateHz: 10,
    batchSize: 50,
    seed: 42,
    burstEverySec: 12,
    burstDurationSec: 3,
    dropoutEverySec: 18,
    dropoutDurationSec: 6,
    lateSeconds: 3600,
    outOfOrderFraction: 0.2,
    waveform: 'sine',
    amplitude: 10,
    periodSec: 5,
    dutyCycle: 0.1,
}

function randomKey(prefix: string): string {
    const cryptoAny = (globalThis as any)?.crypto as Crypto | undefined
    if (cryptoAny?.randomUUID) return `${prefix}_${cryptoAny.randomUUID()}`
    return `${prefix}_${Math.random().toString(16).slice(2)}_${Date.now()}`
}

function hashStringToUint32(str: string): number {
    // FNV-1a 32bit
    let h = 2166136261
    for (let i = 0; i < str.length; i++) {
        h ^= str.charCodeAt(i)
        h = Math.imul(h, 16777619)
    }
    return h >>> 0
}

function isScenario(value: unknown): value is Scenario {
    return value === 'steady' || value === 'bursts' || value === 'dropout' || value === 'out_of_order' || value === 'late_data'
}

function isWaveform(value: unknown): value is Waveform {
    return value === 'sine' || value === 'pulses' || value === 'saw'
}

function createEmptySensor(): SensorConfig {
    return {
        key: randomKey('sensor'),
        label: '',
        sensorId: '',
        sensorToken: '',
        runId: '',
        captureSessionId: '',
        streamSinceId: 0,
        settings: { ...DEFAULT_SETTINGS },
    }
}

function sanitizeNumber(value: unknown, fallback: number): number {
    if (typeof value !== 'number' || !Number.isFinite(value)) return fallback
    return value
}

function sanitizeString(value: unknown, fallback = ''): string {
    return typeof value === 'string' ? value : fallback
}

function sanitizeSettings(raw: unknown): PersistedSettings {
    const s = raw as any
    if (!s || typeof s !== 'object') return { ...DEFAULT_SETTINGS }
    return {
        scenario: isScenario(s?.scenario) ? s.scenario : DEFAULT_SETTINGS.scenario,
        rateHz: sanitizeNumber(s?.rateHz, DEFAULT_SETTINGS.rateHz),
        batchSize: sanitizeNumber(s?.batchSize, DEFAULT_SETTINGS.batchSize),
        seed: sanitizeNumber(s?.seed, DEFAULT_SETTINGS.seed),
        burstEverySec: sanitizeNumber(s?.burstEverySec, DEFAULT_SETTINGS.burstEverySec),
        burstDurationSec: sanitizeNumber(s?.burstDurationSec, DEFAULT_SETTINGS.burstDurationSec),
        dropoutEverySec: sanitizeNumber(s?.dropoutEverySec, DEFAULT_SETTINGS.dropoutEverySec),
        dropoutDurationSec: sanitizeNumber(s?.dropoutDurationSec, DEFAULT_SETTINGS.dropoutDurationSec),
        lateSeconds: sanitizeNumber(s?.lateSeconds, DEFAULT_SETTINGS.lateSeconds),
        outOfOrderFraction: sanitizeNumber(s?.outOfOrderFraction, DEFAULT_SETTINGS.outOfOrderFraction),
        waveform: isWaveform(s?.waveform) ? s.waveform : DEFAULT_SETTINGS.waveform,
        amplitude: sanitizeNumber(s?.amplitude, DEFAULT_SETTINGS.amplitude),
        periodSec: sanitizeNumber(s?.periodSec, DEFAULT_SETTINGS.periodSec),
        dutyCycle: sanitizeNumber(s?.dutyCycle, DEFAULT_SETTINGS.dutyCycle),
    }
}

function sanitizeSensors(value: unknown, fallbackSettings?: PersistedSettings): SensorConfig[] | null {
    if (!Array.isArray(value)) return null
    const defSettings = fallbackSettings || DEFAULT_SETTINGS
    const sensors = value
        .map((raw) => {
            const r = raw as any
            const key = sanitizeString(r?.key)
            if (!key) return null
            return {
                key,
                label: sanitizeString(r?.label),
                sensorId: sanitizeString(r?.sensorId),
                sensorToken: sanitizeString(r?.sensorToken),
                runId: sanitizeString(r?.runId),
                captureSessionId: sanitizeString(r?.captureSessionId),
                streamSinceId: Math.max(0, Math.floor(sanitizeNumber(r?.streamSinceId, 0))),
                settings: r?.settings && typeof r.settings === 'object'
                    ? sanitizeSettings(r.settings)
                    : { ...defSettings },
            } satisfies SensorConfig
        })
        .filter(Boolean) as SensorConfig[]
    return sensors.length ? sensors : null
}

function loadPersistedState(): PersistedStateV2 | null {
    if (typeof window === 'undefined') return null
    const raw = window.localStorage.getItem(STORAGE_KEY)
    if (!raw) return null
    const parsed = safeJsonParse(raw) as any
    if (!parsed || typeof parsed !== 'object') return null

    if (parsed.version === 1) {
        // V1 migration: global settings → apply to all sensors
        const globalSettings = sanitizeSettings(parsed.settings)
        const sensors = sanitizeSensors(parsed.sensors, globalSettings) || [createEmptySensor()]
        const selectedSensorKey = typeof parsed.selectedSensorKey === 'string' ? parsed.selectedSensorKey : sensors[0]?.key
        const selectedKeyOk = sensors.some((s) => s.key === selectedSensorKey)
        return {
            version: 2,
            sensors,
            selectedSensorKey: selectedKeyOk ? selectedSensorKey : sensors[0]!.key,
        }
    }

    if (parsed.version === 2) {
        const sensors = sanitizeSensors(parsed.sensors) || [createEmptySensor()]
        const selectedSensorKey = typeof parsed.selectedSensorKey === 'string' ? parsed.selectedSensorKey : sensors[0]?.key
        const selectedKeyOk = sensors.some((s) => s.key === selectedSensorKey)
        return {
            version: 2,
            sensors,
            selectedSensorKey: selectedKeyOk ? selectedSensorKey : sensors[0]!.key,
        }
    }

    return null
}

function sensorIsReady(sensor: SensorConfig): boolean {
    return uuidLike(sensor.sensorId) && sensor.sensorToken.trim().length > 0
}

function sensorDisplayName(sensor: SensorConfig): string {
    const label = sensor.label.trim()
    if (label) return label
    const id = sensor.sensorId.trim()
    if (id) return id.slice(0, 8)
    return sensor.key.slice(0, 8)
}

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
    const initial = useMemo(() => {
        const loaded = loadPersistedState()
        if (loaded) return loaded
        const s = createEmptySensor()
        return { version: 2, sensors: [s], selectedSensorKey: s.key } satisfies PersistedStateV2
    }, [])

    const [sensors, setSensors] = useState<SensorConfig[]>(initial.sensors)
    const [selectedSensorKey, setSelectedSensorKey] = useState<string>(initial.selectedSensorKey)

    const [isRunning, setIsRunning] = useState(false)
    const [sent, setSent] = useState(0)
    const [accepted, setAccepted] = useState(0)
    const [errors, setErrors] = useState(0)
    const [lastHttpStatus, setLastHttpStatus] = useState<number | null>(null)
    const [log, setLog] = useState<string>('')

    const tickRef = useRef<number | null>(null) // setTimeout id

    const seqBySensorRef = useRef<Map<string, number>>(new Map())
    const lastTimestampBySensorRef = useRef<Map<string, number>>(new Map())
    const rngBySensorRef = useRef<Map<string, { seed: number; rng: () => number }>>(new Map())
    const latestRef = useRef<{ sensors: SensorConfig[] }>({ sensors: [] })
    latestRef.current = { sensors }

    const persistTimerRef = useRef<number | null>(null)

    const [streamOn, setStreamOn] = useState(false)
    const streamAbortRef = useRef<AbortController | null>(null)
    const [streamEvents, setStreamEvents] = useState<Record<string, unknown>[]>([])
    const [streamStatus, setStreamStatus] = useState<'idle' | 'connected' | 'error'>('idle')
    const [streamSensorKey, setStreamSensorKey] = useState<string | null>(null)

    function appendLog(line: string) {
        setLog((prev) => {
            const next = `${prev}${prev ? '\n' : ''}${line}`
            // keep last ~300 lines
            const lines = next.split('\n')
            if (lines.length <= 300) return next
            return lines.slice(lines.length - 300).join('\n')
        })
    }

    const selectedSensor = useMemo(() => {
        const found = sensors.find((s) => s.key === selectedSensorKey)
        return found || sensors[0]
    }, [sensors, selectedSensorKey])

    const activeSensors = useMemo(() => sensors.filter(sensorIsReady), [sensors])
    const canSendAny = activeSensors.length > 0

    function getSensorRng(sensorKey: string, seedValue: number): () => number {
        const existing = rngBySensorRef.current.get(sensorKey)
        if (existing && existing.seed === seedValue) return existing.rng
        const mixed = ((seedValue >>> 0) ^ hashStringToUint32(sensorKey)) >>> 0
        const rng = mulberry32(mixed)
        rngBySensorRef.current.set(sensorKey, { seed: seedValue, rng })
        return rng
    }

    function buildReadings(
        sensorKey: string,
        n: number,
        effectiveRateHz: number,
        snapshot: PersistedSettings,
        isContinuous: boolean
    ): TelemetryIngestReading[] {
        const readings: TelemetryIngestReading[] = []

        const now = Date.now()
        const stepMs = 1000 / clamp(effectiveRateHz, 1, 10_000)

        const rng = getSensorRng(sensorKey, snapshot.seed)
        let seq = seqBySensorRef.current.get(sensorKey) ?? 0
        const lastTs = lastTimestampBySensorRef.current.get(sensorKey) ?? now

        // internal monotonic timestamp for steady generation
        let base = Math.max(lastTs, now)
        if (!isContinuous) base = now

        for (let i = 0; i < n; i++) {
            const tMs = base + i * stepMs
            const t = tMs / 1000
            const noise = (rng() - 0.5) * 0.15
            const raw =
                waveformValue(
                    snapshot.waveform,
                    t,
                    clamp(snapshot.amplitude, 0, 1_000_000),
                    clamp(snapshot.periodSec, 0.001, 1_000_000),
                    snapshot.dutyCycle
                ) +
                noise * 10 +
                20
            const phys = raw * 1.0

            let ts = new Date(tMs).toISOString()
            if (snapshot.scenario === 'late_data') ts = new Date(tMs - snapshot.lateSeconds * 1000).toISOString()

            readings.push({
                timestamp: ts,
                raw_value: raw,
                physical_value: phys,
                meta: {
                    seq: seq++,
                    scenario: snapshot.scenario,
                    generated_at: nowIso(),
                },
            })
        }

        seqBySensorRef.current.set(sensorKey, seq)
        lastTimestampBySensorRef.current.set(sensorKey, base + (n - 1) * stepMs)

        if (snapshot.scenario === 'out_of_order') {
            const frac = clamp(snapshot.outOfOrderFraction, 0, 1)
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

    async function sendBatchForSensor(
        sensor: SensorConfig,
        n: number,
        effectiveRateHz: number,
        snapshot: PersistedSettings,
        isContinuous: boolean
    ) {
        const sensorId = sensor.sensorId.trim()
        const sensorToken = sensor.sensorToken.trim()
        if (!uuidLike(sensorId) || !sensorToken) return

        const body: TelemetryIngestBody = {
            sensor_id: sensorId,
            run_id: sensor.runId.trim() || null,
            capture_session_id: sensor.captureSessionId.trim() || null,
            meta: {
                source: 'sensor-simulator-web',
                scenario: snapshot.scenario,
                rate_hz: effectiveRateHz,
                batch_size: n,
                signal: {
                    waveform: snapshot.waveform,
                    amplitude: clamp(snapshot.amplitude, 0, 1_000_000),
                    period_sec: clamp(snapshot.periodSec, 0.001, 1_000_000),
                    duty_cycle: snapshot.waveform === 'pulses' ? clamp(snapshot.dutyCycle, 0, 1) : null,
                },
            },
            readings: buildReadings(sensor.key, n, effectiveRateHz, snapshot, isContinuous),
        }

        const name = sensorDisplayName(sensor)
        appendLog(`[${nowIso()}] POST /api/v1/telemetry sensor=${name} readings=${n}`)
        const t0 = performance.now()
        try {
            const res = await postTelemetry(body, sensorToken)
            const dt = Math.round(performance.now() - t0)
            setLastHttpStatus(res.status)
            if (res.ok) {
                setSent((v) => v + n)
                const parsed = safeJsonParse(res.text) as { accepted?: unknown } | null
                const acc = parsed && typeof parsed.accepted === 'number' ? parsed.accepted : n
                setAccepted((v) => v + acc)
                appendLog(`[${nowIso()}] ✅ ${res.status} sensor=${name} in ${dt}ms: ${res.text}`)
            } else {
                setErrors((v) => v + 1)
                appendLog(`[${nowIso()}] ❌ ${res.status} sensor=${name} in ${dt}ms: ${res.text}`)
            }
        } catch (e: any) {
            setErrors((v) => v + 1)
            appendLog(`[${nowIso()}] ❌ network error sensor=${name}: ${String(e?.message || e)}`)
        }
    }

    function scenarioIsPausedAt(snapshot: PersistedSettings, nowSec: number): boolean {
        if (snapshot.scenario === 'dropout') {
            const cycle = snapshot.dropoutEverySec + snapshot.dropoutDurationSec
            if (cycle <= 0) return false
            const phase = nowSec % cycle
            return phase >= snapshot.dropoutEverySec
        }
        return false
    }

    function scenarioEffectiveRate(snapshot: PersistedSettings, nowSec: number): number {
        if (snapshot.scenario === 'bursts') {
            const cycle = snapshot.burstEverySec + snapshot.burstDurationSec
            if (cycle <= 0) return snapshot.rateHz
            const phase = nowSec % cycle
            if (phase >= snapshot.burstEverySec) return clamp(snapshot.rateHz * 8, 1, 10_000)
        }
        return snapshot.rateHz
    }

    function start() {
        const sensorsNow = latestRef.current.sensors
        const active = sensorsNow.filter(sensorIsReady)
        if (!active.length) return
        setIsRunning(true)
        const summaries = active.map((s) => `${sensorDisplayName(s)}(${s.settings.scenario})`).join(', ')
        appendLog(`[${nowIso()}] ▶️ start sensors=${active.length} [${summaries}]`)

        const startMs = Date.now()

        const tick = async () => {
            const now = Date.now()
            const elapsedSec = Math.floor((now - startMs) / 1000)
            const sensorsSnap = latestRef.current.sensors
            const activeSnap = sensorsSnap.filter(sensorIsReady)

            if (!activeSnap.length) {
                appendLog(`[${nowIso()}] ⚠️ no active sensors — stopping`)
                stop()
                return
            }

            const sends: Promise<void>[] = []
            for (const sensor of activeSnap) {
                const ss = sensor.settings
                if (scenarioIsPausedAt(ss, elapsedSec)) {
                    appendLog(`[${nowIso()}] ⏸️ dropout window sensor=${sensorDisplayName(sensor)}`)
                    continue
                }
                const effRate = scenarioEffectiveRate(ss, elapsedSec)
                // Each tick = 1 second → send exactly effRate readings per tick
                const readingsCount = Math.max(1, Math.round(effRate))
                sends.push(sendBatchForSensor(sensor, readingsCount, effRate, ss, true))
            }

            if (sends.length > 0) await Promise.all(sends)

            // Schedule next tick in 1 second (uniform cadence)
            if (tickRef.current !== null) {
                tickRef.current = window.setTimeout(tick, 1000)
            }
        }

        // kick off immediately
        tickRef.current = window.setTimeout(tick, 0)
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
            if (persistTimerRef.current) window.clearTimeout(persistTimerRef.current)
        }
    }, [])

    useEffect(() => {
        if (sensors.length === 0) {
            const s = createEmptySensor()
            setSensors([s])
            setSelectedSensorKey(s.key)
            return
        }
        if (!sensors.some((s) => s.key === selectedSensorKey)) {
            setSelectedSensorKey(sensors[0]!.key)
        }
    }, [sensors, selectedSensorKey])

    useEffect(() => {
        // Cleanup per-sensor runtime maps when sensors removed
        const keys = new Set(sensors.map((s) => s.key))
        for (const k of seqBySensorRef.current.keys()) if (!keys.has(k)) seqBySensorRef.current.delete(k)
        for (const k of lastTimestampBySensorRef.current.keys()) if (!keys.has(k)) lastTimestampBySensorRef.current.delete(k)
        for (const k of rngBySensorRef.current.keys()) if (!keys.has(k)) rngBySensorRef.current.delete(k)
    }, [sensors])

    useEffect(() => {
        if (typeof window === 'undefined') return
        const payload: PersistedStateV2 = {
            version: 2,
            sensors,
            selectedSensorKey,
        }

        if (persistTimerRef.current) window.clearTimeout(persistTimerRef.current)
        persistTimerRef.current = window.setTimeout(() => {
            try {
                window.localStorage.setItem(STORAGE_KEY, JSON.stringify(payload))
            } catch {
                // ignore
            }
        }, 500)

        return () => {
            if (persistTimerRef.current) window.clearTimeout(persistTimerRef.current)
        }
    }, [sensors, selectedSensorKey])

    function updateSensor(sensorKey: string, patch: Partial<SensorConfig>) {
        setSensors((prev) => prev.map((s) => (s.key === sensorKey ? { ...s, ...patch } : s)))
    }

    function updateSensorSettings(sensorKey: string, patch: Partial<PersistedSettings>) {
        setSensors((prev) =>
            prev.map((s) =>
                s.key === sensorKey ? { ...s, settings: { ...s.settings, ...patch } } : s
            )
        )
    }

    function copySettingsToAll() {
        if (!selectedSensor) return
        const settingsToCopy = { ...selectedSensor.settings }
        setSensors((prev) => prev.map((s) => ({ ...s, settings: { ...settingsToCopy } })))
        appendLog(`[${nowIso()}] 📋 copied settings from "${sensorDisplayName(selectedSensor)}" to all sensors`)
    }

    function addSensor() {
        const s = createEmptySensor()
        // Copy settings from currently selected sensor for convenience
        if (selectedSensor) {
            s.settings = { ...selectedSensor.settings }
        }
        setSensors((prev) => [...prev, s])
        setSelectedSensorKey(s.key)
        appendLog(`[${nowIso()}] ➕ add sensor`)
    }

    function removeSensor(sensorKey: string) {
        setSensors((prev) => prev.filter((s) => s.key !== sensorKey))
        seqBySensorRef.current.delete(sensorKey)
        lastTimestampBySensorRef.current.delete(sensorKey)
        rngBySensorRef.current.delete(sensorKey)
        if (streamSensorKey === sensorKey) disconnectStream()
        appendLog(`[${nowIso()}] ➖ remove sensor`)
    }

    async function connectStream() {
        if (!selectedSensor) return
        const sensorId = selectedSensor.sensorId.trim()
        const sensorToken = selectedSensor.sensorToken.trim()
        if (!uuidLike(sensorId) || !sensorToken) return
        setStreamOn(true)
        setStreamStatus('idle')
        setStreamSensorKey(selectedSensor.key)
        appendLog(`[${nowIso()}] 🔌 stream connect sensor=${sensorDisplayName(selectedSensor)} since_id=${selectedSensor.streamSinceId}`)

        const controller = new AbortController()
        streamAbortRef.current = controller

        const url = new URL(`${TELEMETRY_BASE}/api/v1/telemetry/stream`, window.location.origin)
        url.searchParams.set('sensor_id', sensorId)
        if (selectedSensor.streamSinceId > 0) url.searchParams.set('since_id', String(selectedSensor.streamSinceId))

        try {
            setStreamStatus('connected')
            for await (const ev of sseFetchStream(
                url.toString(),
                {
                    Authorization: `Bearer ${sensorToken}`,
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
                if (typeof maybeId === 'number') updateSensor(selectedSensor.key, { streamSinceId: maybeId })
            }
        } catch (e: any) {
            setStreamStatus('error')
            appendLog(`[${nowIso()}] ⚠️ stream exception: ${String(e?.message || e)}`)
        } finally {
            setStreamOn(false)
            setStreamSensorKey(null)
        }
    }

    function disconnectStream() {
        setStreamOn(false)
        streamAbortRef.current?.abort()
        streamAbortRef.current = null
        setStreamSensorKey(null)
        appendLog(`[${nowIso()}] 🔌 stream disconnect`)
    }

    const pillClass =
        lastHttpStatus === null ? 'pill' : lastHttpStatus >= 200 && lastHttpStatus < 300 ? 'pill ok' : 'pill bad'

    // Shorthand for selected sensor settings (avoids repetition in JSX)
    const ss = selectedSensor?.settings ?? DEFAULT_SETTINGS

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

                    <div className="hint">
                        Параметры сигнала и сценария настраиваются <b>отдельно для каждого датчика</b>.
                        Датчики (включая токены и настройки) сохраняются в <span className="badge">localStorage</span> этого
                        браузера. Ingest отправляет телеметрию во <b>все</b> датчики с валидными <span className="badge">sensor_id</span>{' '}
                        и <span className="badge">token</span>.
                    </div>

                    <div className="sensorTabs" style={{ marginTop: 10 }}>
                        {sensors.map((s, idx) => {
                            const active = s.key === selectedSensorKey
                            const ready = sensorIsReady(s)
                            const title = sensorDisplayName(s) || `Sensor ${idx + 1}`
                            return (
                                <button
                                    key={s.key}
                                    className={`tab ${active ? 'active' : ''} ${ready ? 'ok' : 'warn'}`}
                                    onClick={() => setSelectedSensorKey(s.key)}
                                    title={ready ? `ready — ${s.settings.scenario} / ${s.settings.waveform}` : 'missing sensor_id/token'}
                                    disabled={streamOn && streamSensorKey !== null}
                                >
                                    {title}
                                </button>
                            )
                        })}
                        <button className="tab add" onClick={() => addSensor()} disabled={streamOn}>
                            + Add sensor
                        </button>
                    </div>

                    {selectedSensor && (
                        <>
                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>label (optional)</label>
                                    <input
                                        value={selectedSensor.label}
                                        onChange={(e) => updateSensor(selectedSensor.key, { label: e.target.value })}
                                        placeholder="например: motor-temp"
                                    />
                                    <div className="hint">Для удобства — отображается в логах и вкладках.</div>
                                </div>
                                <div>
                                    <label>sensor_id</label>
                                    <input
                                        value={selectedSensor.sensorId}
                                        onChange={(e) => updateSensor(selectedSensor.key, { sensorId: e.target.value })}
                                        placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                                    />
                                    <div className="hint">UUID датчика из experiment-service.</div>
                                </div>
                            </div>

                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>sensor token (Bearer)</label>
                                    <input
                                        value={selectedSensor.sensorToken}
                                        onChange={(e) => updateSensor(selectedSensor.key, { sensorToken: e.target.value })}
                                        placeholder="token..."
                                    />
                                    <div className="hint">Токен датчика (возвращается при регистрации/rotate-token).</div>
                                </div>
                                <div>
                                    <label>run_id (optional)</label>
                                    <input
                                        value={selectedSensor.runId}
                                        onChange={(e) => updateSensor(selectedSensor.key, { runId: e.target.value })}
                                        placeholder="uuid..."
                                    />
                                </div>
                            </div>

                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>capture_session_id (optional)</label>
                                    <input
                                        value={selectedSensor.captureSessionId}
                                        onChange={(e) => updateSensor(selectedSensor.key, { captureSessionId: e.target.value })}
                                        placeholder="uuid..."
                                    />
                                </div>
                                <div>
                                    <label>status</label>
                                    <input value={sensorIsReady(selectedSensor) ? 'ready' : 'incomplete'} readOnly />
                                    <div className="hint">Для отправки ingest нужны и sensor_id, и token.</div>
                                </div>
                            </div>

                            <div className="actions">
                                <button
                                    className="btn danger"
                                    disabled={sensors.length <= 1 || isRunning || streamOn}
                                    onClick={() => removeSensor(selectedSensor.key)}
                                >
                                    Remove sensor
                                </button>
                                <button
                                    className="btn"
                                    disabled={sensors.length <= 1}
                                    onClick={() => copySettingsToAll()}
                                    title="Копировать настройки сигнала текущего датчика на все остальные"
                                >
                                    Copy settings to all
                                </button>
                            </div>

                            {/* ── Per-sensor signal & scenario settings ── */}
                            <h3 style={{ margin: '18px 0 6px', fontSize: 13, color: 'rgba(255,255,255,0.7)', letterSpacing: '0.3px' }}>
                                Signal & scenario settings
                            </h3>

                            <div className="row">
                                <div>
                                    <label>scenario</label>
                                    <select
                                        value={ss.scenario}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { scenario: e.target.value as Scenario })}
                                    >
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
                                        value={ss.seed}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { seed: Number(e.target.value) })}
                                        placeholder="42"
                                    />
                                    <div className="hint">Один и тот же seed → одинаковый шум/перемешивание.</div>
                                </div>
                            </div>

                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>signal waveform</label>
                                    <select
                                        value={ss.waveform}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { waveform: e.target.value as Waveform })}
                                    >
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
                                        value={ss.amplitude}
                                        min={0}
                                        step={1}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { amplitude: Number(e.target.value) })}
                                    />
                                    <div className="hint">Для sine/saw: размах; для pulses: высота импульса.</div>
                                </div>
                            </div>

                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>period (sec)</label>
                                    <input
                                        type="number"
                                        value={ss.periodSec}
                                        min={0.001}
                                        step={0.1}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { periodSec: Number(e.target.value) })}
                                    />
                                    <div className="hint">Один период сигнала в секундах.</div>
                                </div>
                                <div>
                                    <label>duty cycle (0..1)</label>
                                    <input
                                        type="number"
                                        value={ss.dutyCycle}
                                        min={0}
                                        max={1}
                                        step={0.05}
                                        disabled={ss.waveform !== 'pulses'}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { dutyCycle: Number(e.target.value) })}
                                    />
                                    <div className="hint">Только для pulses: доля периода, когда импульс "вкл".</div>
                                </div>
                            </div>

                            <div className="row" style={{ marginTop: 10 }}>
                                <div>
                                    <label>rate (Hz)</label>
                                    <input
                                        type="number"
                                        value={ss.rateHz}
                                        min={1}
                                        max={10000}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { rateHz: Number(e.target.value) })}
                                    />
                                    <div className="hint">Readings в секунду при continuous-режиме. Отправка каждую секунду.</div>
                                </div>
                                <div>
                                    <label>batch size (manual)</label>
                                    <input
                                        type="number"
                                        value={ss.batchSize}
                                        min={1}
                                        max={10000}
                                        onChange={(e) => updateSensorSettings(selectedSensor.key, { batchSize: Number(e.target.value) })}
                                    />
                                    <div className="hint">Только для кнопки "Send one batch". В ingest лимит: 10k на запрос.</div>
                                </div>
                            </div>

                            {(ss.scenario === 'bursts' || ss.scenario === 'dropout' || ss.scenario === 'late_data' || ss.scenario === 'out_of_order') && (
                                <div className="row" style={{ marginTop: 10 }}>
                                    {ss.scenario === 'bursts' && (
                                        <>
                                            <div>
                                                <label>burst every (sec)</label>
                                                <input
                                                    type="number"
                                                    value={ss.burstEverySec}
                                                    min={1}
                                                    onChange={(e) => updateSensorSettings(selectedSensor.key, { burstEverySec: Number(e.target.value) })}
                                                />
                                            </div>
                                            <div>
                                                <label>burst duration (sec)</label>
                                                <input
                                                    type="number"
                                                    value={ss.burstDurationSec}
                                                    min={1}
                                                    onChange={(e) => updateSensorSettings(selectedSensor.key, { burstDurationSec: Number(e.target.value) })}
                                                />
                                            </div>
                                        </>
                                    )}
                                    {ss.scenario === 'dropout' && (
                                        <>
                                            <div>
                                                <label>dropout every (sec)</label>
                                                <input
                                                    type="number"
                                                    value={ss.dropoutEverySec}
                                                    min={1}
                                                    onChange={(e) => updateSensorSettings(selectedSensor.key, { dropoutEverySec: Number(e.target.value) })}
                                                />
                                            </div>
                                            <div>
                                                <label>dropout duration (sec)</label>
                                                <input
                                                    type="number"
                                                    value={ss.dropoutDurationSec}
                                                    min={1}
                                                    onChange={(e) => updateSensorSettings(selectedSensor.key, { dropoutDurationSec: Number(e.target.value) })}
                                                />
                                            </div>
                                        </>
                                    )}
                                    {ss.scenario === 'late_data' && (
                                        <div>
                                            <label>late seconds (timestamp - N)</label>
                                            <input
                                                type="number"
                                                value={ss.lateSeconds}
                                                min={1}
                                                onChange={(e) => updateSensorSettings(selectedSensor.key, { lateSeconds: Number(e.target.value) })}
                                            />
                                        </div>
                                    )}
                                    {ss.scenario === 'out_of_order' && (
                                        <div>
                                            <label>out-of-order fraction (0..1)</label>
                                            <input
                                                type="number"
                                                value={ss.outOfOrderFraction}
                                                min={0}
                                                max={1}
                                                step={0.05}
                                                onChange={(e) => updateSensorSettings(selectedSensor.key, { outOfOrderFraction: Number(e.target.value) })}
                                            />
                                        </div>
                                    )}
                                </div>
                            )}
                        </>
                    )}

                    <div className="actions">
                        <button className="btn primary" disabled={!canSendAny || isRunning} onClick={() => start()}>
                            Start ingest
                        </button>
                        <button className="btn danger" disabled={!isRunning} onClick={() => stop()}>
                            Stop ingest
                        </button>
                        <button
                            className="btn"
                            disabled={!canSendAny || isRunning}
                            onClick={() => {
                                const sensorsNow = latestRef.current.sensors
                                const active = sensorsNow.filter(sensorIsReady)
                                void Promise.all(
                                    active.map((s) => {
                                        const n = clamp(s.settings.batchSize, 1, 10_000)
                                        const r = clamp(s.settings.rateHz, 1, 10_000)
                                        return sendBatchForSensor(s, n, r, s.settings, false)
                                    })
                                )
                            }}
                        >
                            Send one batch (all sensors)
                        </button>
                        <button
                            className="btn"
                            onClick={() => {
                                setSent(0)
                                setAccepted(0)
                                setErrors(0)
                                setLastHttpStatus(null)
                                setLog('')
                                setStreamEvents([])
                                seqBySensorRef.current.clear()
                                lastTimestampBySensorRef.current.clear()
                                rngBySensorRef.current.clear()
                                appendLog(`[${nowIso()}] 🧹 reset counters/log`)
                            }}
                        >
                            Reset
                        </button>
                    </div>

                    <div className="kpis">
                        <div className="kpi">
                            <div className="label">sensors (active/total)</div>
                            <div className="value">
                                {activeSensors.length}/{sensors.length}
                            </div>
                        </div>
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
                            <label>sensor</label>
                            <input
                                value={selectedSensor ? sensorDisplayName(selectedSensor) : ''}
                                readOnly
                            />
                            <div className="hint">Stream подключается к выбранному датчику (вкладка выше).</div>
                        </div>
                        <div>
                            <label>status</label>
                            <input value={streamStatus} readOnly />
                            <div className="hint">Connected/idle/error.</div>
                        </div>
                    </div>

                    <div className="row" style={{ marginTop: 10 }}>
                        <div>
                            <label>since_id</label>
                            <input
                                type="number"
                                value={selectedSensor?.streamSinceId ?? 0}
                                min={0}
                                onChange={(e) =>
                                    selectedSensor ? updateSensor(selectedSensor.key, { streamSinceId: Number(e.target.value) }) : null
                                }
                                disabled={!selectedSensor}
                            />
                            <div className="hint">Последний увиденный telemetry_records.id (сохраняется отдельно для каждого датчика).</div>
                        </div>
                        <div>
                            <label>note</label>
                            <input value={streamOn ? 'stream is pinned to selected sensor' : '—'} readOnly />
                        </div>
                    </div>

                    <div className="actions">
                        <button
                            className="btn good"
                            disabled={!selectedSensor || !sensorIsReady(selectedSensor) || streamOn}
                            onClick={() => void connectStream()}
                        >
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
