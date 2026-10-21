import { describe, it, expect, vi, afterEach } from 'vitest'
import { formatShortDateTime } from '../../utils/format'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import SensorBackfillSection from './SensorBackfillSection'
import type { BackfillTask, BackfillTasksListResponse } from '../../types'

const makeTask = (overrides: Partial<BackfillTask>): BackfillTask =>
    ({
        id: 'task-1',
        sensor_id: 'sensor-1',
        conversion_profile_id: 'profile-1',
        status: 'completed',
        total_records: 100,
        processed_records: 50,
        error_message: null,
        created_at: '2024-03-05T10:20:00Z',
        started_at: null,
        completed_at: null,
        ...overrides,
    }) as unknown as BackfillTask

const list = (tasks: BackfillTask[]): BackfillTasksListResponse =>
    ({ backfill_tasks: tasks, total: tasks.length }) as unknown as BackfillTasksListResponse

describe('SensorBackfillSection', () => {
    afterEach(() => {
        vi.restoreAllMocks()
    })

    it('shows loading and disables button without active profile', () => {
        const mutation = { mutate: vi.fn(), isPending: false }
        render(
            <SensorBackfillSection
                backfillData={undefined}
                isLoadingBackfill
                startBackfillMutation={mutation}
                hasActiveProfile={false}
            />
        )
        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
        const btn = screen.getByRole('button', { name: 'Запустить пересчёт' })
        expect(btn).toBeDisabled()
        expect(btn).toHaveAttribute('title', 'Нет активного профиля')
        expect(screen.queryByText('Нет задач пересчёта')).not.toBeInTheDocument()
    })

    it('shows pending label and disables while pending', () => {
        render(
            <SensorBackfillSection
                backfillData={list([])}
                isLoadingBackfill={false}
                startBackfillMutation={{ mutate: vi.fn(), isPending: true }}
                hasActiveProfile
            />
        )
        const btn = screen.getByRole('button', { name: 'Запуск...' })
        expect(btn).toBeDisabled()
        expect(btn).toHaveAttribute('title', '')
        expect(screen.getByText('Нет задач пересчёта')).toBeInTheDocument()
    })

    it('runs mutation only when confirmed', async () => {
        const user = userEvent.setup()
        const mutate = vi.fn()
        const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
        render(
            <SensorBackfillSection
                backfillData={undefined}
                isLoadingBackfill={false}
                startBackfillMutation={{ mutate, isPending: false }}
                hasActiveProfile
            />
        )
        const btn = screen.getByRole('button', { name: 'Запустить пересчёт' })
        await user.click(btn)
        expect(mutate).not.toHaveBeenCalled()
        await user.click(btn)
        expect(mutate).toHaveBeenCalledTimes(1)
        expect(confirmSpy).toHaveBeenCalledTimes(2)
        expect(screen.queryByRole('table')).not.toBeInTheDocument()
    })

    it('renders tasks with progress, statuses and error messages', () => {
        const longError = 'x'.repeat(60)
        const tasks = [
            makeTask({ id: 't1', status: 'pending', total_records: 200, processed_records: 50 }),
            makeTask({ id: 't2', status: 'running', total_records: null, processed_records: 0 }),
            makeTask({ id: 't3', status: 'failed', total_records: 0, processed_records: 0, error_message: longError }),
            makeTask({ id: 't4', status: 'completed', error_message: 'short error' }),
        ]
        const { container } = render(
            <SensorBackfillSection
                backfillData={list(tasks)}
                isLoadingBackfill={false}
                startBackfillMutation={{ mutate: vi.fn(), isPending: false }}
                hasActiveProfile
            />
        )
        expect(screen.getByText('Ожидает')).toHaveClass('badge-secondary')
        expect(screen.getByText('Выполняется')).toHaveClass('badge-info')
        expect(screen.getByText('Ошибка', { selector: 'span' })).toHaveClass('badge-danger')
        expect(screen.getByText('Завершён')).toHaveClass('badge-success')
        expect(screen.getByText('50 / 200')).toBeInTheDocument()
        expect(screen.getByText('0 / 0')).toBeInTheDocument()
        expect(screen.getByText('—')).toBeInTheDocument()
        expect(screen.getByText('x'.repeat(50) + '...')).toHaveAttribute('title', longError)
        expect(screen.getByText('short error')).toBeInTheDocument()
        expect(screen.getAllByText(formatShortDateTime('2024-03-05T10:20:00Z'))).toHaveLength(4)
        const bars = Array.from(container.querySelectorAll<HTMLElement>('div[style*="transition"]'))
        expect(bars.map((b) => b.style.width)).toEqual(['25%', '0%', '50%'])
        expect(bars[1].style.background).toBe('var(--lp-error)')
        expect(bars[0].style.background).toBe('var(--lp-primary)')
        expect(screen.queryByText('Нет задач пересчёта')).not.toBeInTheDocument()
    })
})
