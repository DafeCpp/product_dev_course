import { describe, it, expect, vi, afterEach } from 'vitest'
import { formatDateTime } from '../../utils/format'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import SensorConversionProfiles from './SensorConversionProfiles'
import type { ConversionProfile, ConversionProfilesListResponse } from '../../types'

const profile = (overrides: Partial<ConversionProfile>): ConversionProfile =>
    ({
        id: 'p1',
        sensor_id: 's1',
        project_id: 'proj',
        version: 'v1',
        kind: 'linear',
        payload: {},
        status: 'draft',
        created_at: '2024-04-01T12:30:00Z',
        updated_at: '2024-04-01T12:30:00Z',
        ...overrides,
    }) as unknown as ConversionProfile

const list = (profiles: ConversionProfile[]): ConversionProfilesListResponse =>
    ({ conversion_profiles: profiles, total: profiles.length }) as unknown as ConversionProfilesListResponse

describe('SensorConversionProfiles', () => {
    afterEach(() => {
        vi.restoreAllMocks()
    })

    it('shows loading state and create button works', async () => {
        const user = userEvent.setup()
        const onCreate = vi.fn()
        render(
            <SensorConversionProfiles
                profilesData={undefined}
                isLoadingProfiles
                publishProfileMutation={{ mutate: vi.fn(), isPending: false }}
                onCreateProfile={onCreate}
            />
        )
        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
        await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
        expect(onCreate).toHaveBeenCalled()
    })

    it('shows empty state', () => {
        render(
            <SensorConversionProfiles
                profilesData={list([])}
                isLoadingProfiles={false}
                publishProfileMutation={{ mutate: vi.fn(), isPending: false }}
                onCreateProfile={vi.fn()}
            />
        )
        expect(screen.getByText('Нет профилей преобразования')).toBeInTheDocument()
    })

    it('renders nothing when not loading and no data', () => {
        render(
            <SensorConversionProfiles
                profilesData={undefined}
                isLoadingProfiles={false}
                publishProfileMutation={{ mutate: vi.fn(), isPending: false }}
                onCreateProfile={vi.fn()}
            />
        )
        expect(screen.queryByRole('table')).not.toBeInTheDocument()
        expect(screen.queryByText('Нет профилей преобразования')).not.toBeInTheDocument()
    })

    it('renders profiles of all kinds and statuses and publishes on confirm', async () => {
        const user = userEvent.setup()
        const mutate = vi.fn()
        vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
        const { container } = render(
            <SensorConversionProfiles
                profilesData={list([
                    profile({ id: 'p1', version: 'v1', kind: 'linear', status: 'draft' }),
                    profile({ id: 'p2', version: 'v2', kind: 'polynomial', status: 'scheduled' }),
                    profile({ id: 'p3', version: 'v3', kind: 'lookup_table', status: 'active' }),
                    profile({ id: 'p4', version: 'v4', kind: 'custom_kind' as ConversionProfile['kind'], status: 'deprecated' }),
                ])}
                isLoadingProfiles={false}
                publishProfileMutation={{ mutate, isPending: false }}
                onCreateProfile={vi.fn()}
            />
        )
        expect(screen.getByText('Линейное')).toBeInTheDocument()
        expect(screen.getByText('Полиномиальное')).toBeInTheDocument()
        expect(screen.getByText('Таблица')).toBeInTheDocument()
        expect(screen.getByText('custom_kind')).toBeInTheDocument()
        expect(screen.getByText('Черновик')).toHaveClass('badge-secondary')
        expect(screen.getByText('Запланирован')).toHaveClass('badge-info')
        expect(screen.getByText('Активен')).toHaveClass('badge-success')
        expect(screen.getByText('Устаревший')).toHaveClass('badge-muted')
        expect(screen.getByText('Активный профиль')).toBeInTheDocument()
        expect(container.querySelectorAll('tr.row-active')).toHaveLength(1)
        expect(screen.getAllByText(formatDateTime('2024-04-01T12:30:00Z'))).toHaveLength(4)

        const publishButtons = screen.getAllByRole('button', { name: 'Опубликовать' })
        expect(publishButtons).toHaveLength(2)
        await user.click(publishButtons[0])
        expect(mutate).not.toHaveBeenCalled()
        await user.click(publishButtons[1])
        expect(mutate).toHaveBeenCalledWith('p2')
    })

    it('shows pending publish label', () => {
        render(
            <SensorConversionProfiles
                profilesData={list([profile({})])}
                isLoadingProfiles={false}
                publishProfileMutation={{ mutate: vi.fn(), isPending: true }}
                onCreateProfile={vi.fn()}
            />
        )
        expect(screen.getByRole('button', { name: 'Публикация...' })).toBeDisabled()
    })
})
