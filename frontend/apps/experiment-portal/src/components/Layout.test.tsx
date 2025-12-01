import { describe, expect, it } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen } from '@testing-library/react'

import Layout from './Layout'

describe('Layout', () => {
    it('renders header links and page content', () => {
        render(
            <MemoryRouter initialEntries={['/experiments']}>
                <Layout>
                    <div>Test content</div>
                </Layout>
            </MemoryRouter>
        )

        expect(
            screen.getByRole('heading', {
                name: /experiment tracking/i
            })
        ).toBeInTheDocument()
        expect(screen.getByText('Test content')).toBeInTheDocument()
        expect(screen.getByRole('link', { name: /эксперименты/i })).toHaveClass('active')
    })
})
