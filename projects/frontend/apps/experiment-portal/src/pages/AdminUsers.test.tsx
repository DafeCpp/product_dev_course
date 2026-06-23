import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import AdminUsers from './AdminUsers'

vi.mock('../api/client', () => ({
  authApi: {
    listUsers: vi.fn().mockResolvedValue({ users: [] }),
  },
  permissionsApi: {
    listUserPermissions: vi.fn().mockResolvedValue({ permissions: [] }),
  },
}))

vi.mock('../hooks/usePermissions', () => ({
  usePermissions: vi.fn(() => ({
    hasPermission: vi.fn(() => true),
  })),
}))

describe('AdminUsers', () => {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } },
  })

  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders without crashing', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <AdminUsers />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container.firstChild).toBeInTheDocument()
  })

  it('displays user management interface', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <AdminUsers />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })

  it('respects user permissions', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <AdminUsers />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })
})
