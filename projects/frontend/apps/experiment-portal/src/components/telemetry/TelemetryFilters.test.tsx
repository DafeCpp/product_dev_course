import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { vi } from 'vitest'
import TelemetryFilters from './TelemetryFilters'

describe('TelemetryFilters', () => {
    it('clears dependent selections through its callbacks', async () => {
        const user = userEvent.setup()
        const onProjectChange = vi.fn()
        render(<TelemetryFilters projectId="project-1" experimentId="experiment-1" runId="run-1" viewMode="live" projects={[{ id: 'project-1', name: 'Проект' }, { id: 'project-2', name: 'Другой проект' }]} experiments={[{ id: 'experiment-1', name: 'Эксперимент' }]} runs={[{ id: 'run-1', name: 'Пуск' }]} projectsLoading={false} experimentsLoading={false} runsLoading={false} onProjectChange={onProjectChange} onExperimentChange={vi.fn()} onRunChange={vi.fn()} onViewModeChange={vi.fn()} />)

        await user.click(screen.getByLabelText('Проект'))
        await user.click(screen.getByRole('option', { name: 'Другой проект' }))
        expect(onProjectChange).toHaveBeenCalledWith('project-2')
    })
})
