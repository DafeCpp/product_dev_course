import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import TelemetryStreamModal from './TelemetryStreamModal'
import { telemetryApi } from '../api/client'

vi.mock('../api/client', () => ({
    telemetryApi: {
        stream: vi.fn(),
    },
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

describe('TelemetryStreamModal', () => {
    const onClose = vi.fn()

    beforeEach(() => {
        vi.clearAllMocks()
        onClose.mockClear()
    })

    it('renders and streams telemetry events', async () => {
        const user = userEvent.setup()

        const payload = {
            id: 1,
            sensor_id: '00000000-0000-0000-0000-000000000001',
            project_id: '00000000-0000-0000-0000-000000000002',
            timestamp: '2026-01-01T00:00:00Z',
            raw_value: 1.23,
            physical_value: null,
            run_id: null,
            capture_session_id: null,
            meta: {},
        }

        const sse = [
            ': heartbeat\n\n',
            'event: telemetry\n',
            `data: ${JSON.stringify(payload)}\n\n`,
        ]

        vi.mocked(telemetryApi.stream).mockResolvedValue({
            response: {
                ok: true,
                status: 200,
                statusText: 'OK',
                headers: new Headers(),
                body: makeSSEStream(sse),
            },
            debug: { url: 'http://example/stream', headers: { 'X-Trace-Id': 't', 'X-Request-Id': 'r' }, method: 'GET' },
        } as any)

        render(
            <TelemetryStreamModal
                sensorId="00000000-0000-0000-0000-000000000001"
                isOpen={true}
                onClose={onClose}
            />
        )

        await user.click(screen.getByRole('button', { name: /старт/i }))

        await waitFor(() => {
            expect(screen.getByText(/events:\s*1/i)).toBeInTheDocument()
            expect(screen.getByText(/#1/i)).toBeInTheDocument()
        })

        expect(screen.getByRole('img', { name: /telemetry sparkline/i })).toBeInTheDocument()
        expect(vi.mocked(telemetryApi.stream)).toHaveBeenCalledTimes(1)
    })

    it('auto-reconnects after a stream drop, resuming from the last received cursor', async () => {
        const user = userEvent.setup()

        const payload = {
            id: 7,
            sensor_id: '00000000-0000-0000-0000-000000000001',
            project_id: '00000000-0000-0000-0000-000000000002',
            timestamp: '2026-01-01T00:00:07Z',
            raw_value: 7,
            physical_value: null,
            run_id: null,
            capture_session_id: null,
            meta: {},
        }

        vi.mocked(telemetryApi.stream)
            .mockResolvedValueOnce({
                response: {
                    ok: true,
                    status: 200,
                    statusText: 'OK',
                    headers: new Headers(),
                    body: makeSSEStream(['event: telemetry\n', `data: ${JSON.stringify(payload)}\n\n`]),
                },
                debug: { url: 'http://example/stream', headers: {}, method: 'GET' },
            } as any)
            .mockResolvedValueOnce({
                response: {
                    ok: true,
                    status: 200,
                    statusText: 'OK',
                    headers: new Headers(),
                    body: makeSSEStream([]),
                },
                debug: { url: 'http://example/stream', headers: {}, method: 'GET' },
            } as any)

        render(
            <TelemetryStreamModal
                sensorId="00000000-0000-0000-0000-000000000001"
                isOpen={true}
                onClose={onClose}
            />
        )

        await user.click(screen.getByRole('button', { name: /старт/i }))

        await waitFor(
            () => {
                expect(vi.mocked(telemetryApi.stream)).toHaveBeenCalledTimes(2)
            },
            { timeout: 3000 },
        )
        expect(vi.mocked(telemetryApi.stream).mock.calls[1][0]).toEqual(
            expect.objectContaining({ since_id: 7, since_ts: '2026-01-01T00:00:07Z' })
        )
    })
})

