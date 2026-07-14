import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import TelemetryPanel from './TelemetryPanel'
import { telemetryApi } from '../api/client'
import type { Sensor } from '../types'

vi.mock('../api/client', () => ({
    telemetryApi: {
        stream: vi.fn(),
    },
}))

vi.mock('plotly.js-dist-min', () => ({
    default: { react: vi.fn(), purge: vi.fn(), relayout: vi.fn(), Plots: { resize: vi.fn() } },
}))

function makeSSEStream(chunks: string[]) {
    const enc = new TextEncoder()
    return new ReadableStream<Uint8Array>({
        start(controller) {
            for (const chunk of chunks) controller.enqueue(enc.encode(chunk))
            controller.close()
        },
    })
}

function makeSensor(overrides: Partial<Sensor> = {}): Sensor {
    return {
        id: 's1',
        project_id: 'p1',
        name: 'Sensor 1',
        type: 'temperature',
        input_unit: 'raw',
        display_unit: 'C',
        status: 'active',
        created_at: '2026-01-01T00:00:00Z',
        updated_at: '2026-01-01T00:00:00Z',
        ...overrides,
    }
}

function makeStreamResponse(chunks: string[]) {
    return {
        response: {
            ok: true,
            status: 200,
            statusText: 'OK',
            headers: new Headers(),
            body: makeSSEStream(chunks),
        },
    } as any
}

describe('TelemetryPanel', () => {
    const onRemove = vi.fn()

    beforeEach(() => {
        vi.clearAllMocks()
        window.localStorage.clear()
    })

    it('starts a single-sensor stream and renders received records', async () => {
        const user = userEvent.setup()
        const payload = {
            id: 1,
            sensor_id: 's1',
            project_id: 'p1',
            timestamp: '2026-01-01T00:00:01Z',
            raw_value: 1.5,
            physical_value: 1.5,
            run_id: null,
            capture_session_id: null,
            meta: {},
        }

        vi.mocked(telemetryApi.stream).mockResolvedValue(
            makeStreamResponse(['event: telemetry\n', `data: ${JSON.stringify(payload)}\n\n`]),
        )

        render(
            <TelemetryPanel
                panelId="panel-1"
                title="Panel"
                sensors={[makeSensor()]}
                onRemove={onRemove}
            />,
        )

        await user.click(screen.getByRole('button', { name: /настройки/i }))
        await user.click(screen.getByRole('button', { name: /добавить сенсор/i }))
        await user.click(await screen.findByRole('option', { name: /sensor 1/i }))

        await user.click(screen.getByRole('button', { name: /старт/i }))

        await waitFor(() => expect(telemetryApi.stream).toHaveBeenCalledTimes(1))
        expect(telemetryApi.stream).toHaveBeenCalledWith(
            expect.objectContaining({ sensor_id: 's1' }),
        )

        await waitFor(() => expect(screen.getByRole('button', { name: /стоп/i })).toBeInTheDocument())
    })

    it('auto-reconnects a dropped sensor stream, resuming from the last cursor', async () => {
        const user = userEvent.setup()
        const payload = {
            id: 9,
            sensor_id: 's1',
            project_id: 'p1',
            timestamp: '2026-01-01T00:00:09Z',
            raw_value: 9,
            physical_value: 9,
            run_id: null,
            capture_session_id: null,
            meta: {},
        }

        vi.mocked(telemetryApi.stream)
            .mockResolvedValueOnce(
                makeStreamResponse(['event: telemetry\n', `data: ${JSON.stringify(payload)}\n\n`]),
            )
            .mockResolvedValueOnce(makeStreamResponse([]))

        render(
            <TelemetryPanel
                panelId="panel-2"
                title="Panel"
                sensors={[makeSensor()]}
                onRemove={onRemove}
            />,
        )

        await user.click(screen.getByRole('button', { name: /настройки/i }))
        await user.click(screen.getByRole('button', { name: /добавить сенсор/i }))
        await user.click(await screen.findByRole('option', { name: /sensor 1/i }))
        await user.click(screen.getByRole('button', { name: /старт/i }))

        await waitFor(
            () => expect(telemetryApi.stream).toHaveBeenCalledTimes(2),
            { timeout: 3000 },
        )
        expect(vi.mocked(telemetryApi.stream).mock.calls[1][0]).toEqual(
            expect.objectContaining({ since_id: 9, since_ts: '2026-01-01T00:00:09Z' }),
        )
    })

    it('stops all sensor streams when Стоп is clicked', async () => {
        const user = userEvent.setup()
        vi.mocked(telemetryApi.stream).mockResolvedValue(makeStreamResponse([]))

        render(
            <TelemetryPanel
                panelId="panel-3"
                title="Panel"
                sensors={[makeSensor()]}
                onRemove={onRemove}
            />,
        )

        await user.click(screen.getByRole('button', { name: /настройки/i }))
        await user.click(screen.getByRole('button', { name: /добавить сенсор/i }))
        await user.click(await screen.findByRole('option', { name: /sensor 1/i }))
        await user.click(screen.getByRole('button', { name: /старт/i }))

        await waitFor(() => expect(screen.getByRole('button', { name: /стоп/i })).toBeInTheDocument())
        await user.click(screen.getByRole('button', { name: /стоп/i }))

        await waitFor(() => expect(screen.getByRole('button', { name: /старт/i })).toBeInTheDocument())

        const callsBeforeWait = vi.mocked(telemetryApi.stream).mock.calls.length
        await new Promise((r) => setTimeout(r, 1200))
        expect(vi.mocked(telemetryApi.stream).mock.calls.length).toBe(callsBeforeWait)
    })
})
