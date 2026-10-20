import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { format } from 'date-fns'
import SensorErrorLog from './SensorErrorLog'
import type { SensorErrorEntry, SensorErrorLogResponse } from '../../types'

const entry = (overrides: Partial<SensorErrorEntry>): SensorErrorEntry => ({
    id: 1,
    sensor_id: 's1',
    occurred_at: '2024-02-01T08:09:10Z',
    error_code: 'rate_limited',
    error_message: 'Too many',
    endpoint: '/ingest',
    readings_count: 5,
    meta: {},
    ...overrides,
})

const data = (entries: SensorErrorEntry[], total: number): SensorErrorLogResponse => ({
    sensor_id: 's1',
    entries,
    total,
    limit: 2,
    offset: 0,
})

describe('SensorErrorLog', () => {
    it('shows loading without badge when no data', () => {
        render(
            <SensorErrorLog
                errorLogData={undefined}
                isLoadingErrorLog
                errorLogPage={0}
                setErrorLogPage={vi.fn()}
                errorLogLimit={2}
            />
        )
        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
        expect(screen.queryByText('Ошибок нет')).not.toBeInTheDocument()
    })

    it('shows empty state with no total badge', () => {
        const { container } = render(
            <SensorErrorLog
                errorLogData={data([], 0)}
                isLoadingErrorLog={false}
                errorLogPage={0}
                setErrorLogPage={vi.fn()}
                errorLogLimit={2}
            />
        )
        expect(screen.getByText('Ошибок нет')).toBeInTheDocument()
        expect(container.querySelector('h3 .badge')).toBeNull()
    })

    it('renders entries with badge colors and fallbacks, no pagination when fits', () => {
        const entries = [
            entry({ id: 1, error_code: 'rate_limited' }),
            entry({ id: 2, error_code: 'unauthorized', readings_count: null, error_message: null }),
            entry({ id: 3, error_code: 'validation_error' }),
            entry({ id: 4, error_code: 'other_code' }),
        ]
        const { container } = render(
            <SensorErrorLog
                errorLogData={data(entries, 4)}
                isLoadingErrorLog={false}
                errorLogPage={0}
                setErrorLogPage={vi.fn()}
                errorLogLimit={10}
            />
        )
        expect(container.querySelector('h3 .badge')).toHaveTextContent('4')
        expect(screen.getByText('rate_limited')).toHaveClass('badge-warning')
        expect(screen.getByText('unauthorized')).toHaveClass('badge-danger')
        expect(screen.getByText('validation_error')).toHaveClass('badge-info')
        expect(screen.getByText('other_code')).toHaveClass('badge-secondary')
        expect(screen.getAllByText('—')).toHaveLength(2)
        expect(screen.getAllByText(format(new Date('2024-02-01T08:09:10Z'), 'dd MMM HH:mm:ss'))).toHaveLength(4)
        expect(screen.queryByText('← Назад')).not.toBeInTheDocument()
    })

    it('paginates on first page', async () => {
        const user = userEvent.setup()
        const setPage = vi.fn()
        render(
            <SensorErrorLog
                errorLogData={data([entry({}), entry({ id: 2 })], 5)}
                isLoadingErrorLog={false}
                errorLogPage={0}
                setErrorLogPage={setPage}
                errorLogLimit={2}
            />
        )
        expect(screen.getByText('1–2 из 5')).toBeInTheDocument()
        expect(screen.getByRole('button', { name: '← Назад' })).toBeDisabled()
        await user.click(screen.getByRole('button', { name: 'Вперёд →' }))
        const updater = setPage.mock.calls[0][0] as (p: number) => number
        expect(updater(0)).toBe(1)
    })

    it('paginates on last page', async () => {
        const user = userEvent.setup()
        const setPage = vi.fn()
        render(
            <SensorErrorLog
                errorLogData={data([entry({})], 5)}
                isLoadingErrorLog={false}
                errorLogPage={2}
                setErrorLogPage={setPage}
                errorLogLimit={2}
            />
        )
        expect(screen.getByText('5–5 из 5')).toBeInTheDocument()
        expect(screen.getByRole('button', { name: 'Вперёд →' })).toBeDisabled()
        await user.click(screen.getByRole('button', { name: '← Назад' }))
        const updater = setPage.mock.calls[0][0] as (p: number) => number
        expect(updater(2)).toBe(1)
    })
})
