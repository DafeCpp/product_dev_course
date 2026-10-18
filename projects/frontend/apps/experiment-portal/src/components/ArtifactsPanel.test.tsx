import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ArtifactsPanel from './ArtifactsPanel'
import { artifactsApi } from '../api/client'

vi.mock('../api/client', () => ({
  artifactsApi: {
    list: vi.fn(), create: vi.fn(), delete: vi.fn(), approve: vi.fn(),
    requestUploadUrl: vi.fn(), getDownloadUrl: vi.fn(),
  },
}))
vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(), notifySuccess: vi.fn(), notifyInfo: vi.fn(), notifyWarning: vi.fn(),
}))

function art(over: Record<string, unknown> = {}) {
  return {
    id: 'a1', run_id: 'r1', type: 'model', uri: 's3://bucket/dir/model.bin', size_bytes: 2048,
    checksum: 'sha256:abc', approved_by: null, created_at: '2026-01-02T10:00:00Z', ...over,
  }
}

function renderPanel(props: { isOwner?: boolean } = {}) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <ArtifactsPanel runId="r1" projectId="p1" {...props} />
    </QueryClientProvider>
  )
}

async function pickOption(user: ReturnType<typeof userEvent.setup>, combobox: HTMLElement, name: string) {
  await user.click(combobox)
  await user.click(await screen.findByRole('option', { name }))
}

describe('ArtifactsPanel', () => {
  let openSpy: ReturnType<typeof vi.spyOn>

  beforeEach(() => {
    vi.clearAllMocks()
    openSpy = vi.spyOn(window, 'open').mockImplementation(() => null)
    vi.mocked(artifactsApi.list).mockResolvedValue({ artifacts: [art()], total: 1 } as never)
  })
  afterEach(() => {
    openSpy.mockRestore()
    vi.unstubAllGlobals()
  })

  it('renders rows with formatted size, checksum and s3 file name', async () => {
    renderPanel()
    expect(await screen.findByText('model.bin')).toBeInTheDocument()
    expect(screen.getByText('2.0 KB')).toBeInTheDocument()
    expect(screen.getByText('sha256:abc')).toBeInTheDocument()
    expect(artifactsApi.list).toHaveBeenCalledWith('r1', { type: undefined, limit: 10, offset: 0 })
  })

  it('formats sizes in every unit and shows a dash for missing values', async () => {
    vi.mocked(artifactsApi.list).mockResolvedValue({
      artifacts: [
        art({ id: 'b', size_bytes: 12, uri: 'https://x.test/b', type: 'dataset', checksum: null }),
        art({ id: 'c', size_bytes: 3 * 1024 * 1024, uri: 'https://x.test/c', type: 'plot' }),
        art({ id: 'd', size_bytes: 5 * 1024 * 1024 * 1024, uri: 'https://x.test/d', type: 'log' }),
        art({ id: 'e', size_bytes: null, uri: 'https://x.test/e', type: 'config', approved_by: 'alice' }),
        art({ id: 'f', size_bytes: 0, uri: 'https://x.test/f', type: 'weird' }),
      ],
      total: 5,
    } as never)
    renderPanel()
    expect(await screen.findByText('12 B')).toBeInTheDocument()
    expect(screen.getByText('3.0 MB')).toBeInTheDocument()
    expect(screen.getByText('5.00 GB')).toBeInTheDocument()
    expect(screen.getByText('alice')).toBeInTheDocument()
    expect(screen.getByRole('link', { name: 'https://x.test/b' })).toHaveAttribute('href', 'https://x.test/b')
  })

  it('shows a loading spinner, then an error state', async () => {
    vi.mocked(artifactsApi.list).mockRejectedValue(new Error('boom'))
    renderPanel()
    expect(screen.getByRole('progressbar')).toBeInTheDocument()
    expect(await screen.findByText('Не удалось загрузить артефакты')).toBeInTheDocument()
  })

  it('shows the empty state, with a type-specific message when filtered', async () => {
    const user = userEvent.setup()
    vi.mocked(artifactsApi.list).mockResolvedValue({ artifacts: [], total: 0 } as never)
    renderPanel()
    expect(await screen.findByText('Нет артефактов')).toBeInTheDocument()
    await pickOption(user, screen.getAllByRole('combobox')[0], 'plot')
    expect(await screen.findByText('Нет артефактов типа "plot"')).toBeInTheDocument()
    expect(artifactsApi.list).toHaveBeenLastCalledWith('r1', { type: 'plot', limit: 10, offset: 0 })
  })

  it('paginates and resets the page on filter change', async () => {
    const user = userEvent.setup()
    vi.mocked(artifactsApi.list).mockResolvedValue({ artifacts: [art()], total: 25 } as never)
    renderPanel()
    await screen.findByText('model.bin')
    await user.click(screen.getByRole('button', { name: /next page/i }))
    await waitFor(() => expect(artifactsApi.list).toHaveBeenLastCalledWith('r1', { type: undefined, limit: 10, offset: 10 }))
    await pickOption(user, screen.getAllByRole('combobox')[0], 'log')
    await waitFor(() => expect(artifactsApi.list).toHaveBeenLastCalledWith('r1', { type: 'log', limit: 10, offset: 0 }))
  })

  describe('add dialog', () => {
    it('creates an artifact with parsed metadata and closes', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.create).mockResolvedValue(art() as never)
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      const dialog = await screen.findByRole('dialog')
      expect(within(dialog).getByRole('button', { name: 'Add' })).toBeDisabled()
      await pickOption(user, within(dialog).getByRole('combobox'), 'dataset')
      await user.type(within(dialog).getByLabelText(/URI/), 's3://b/x.csv')
      await user.type(within(dialog).getByLabelText(/Checksum/), 'sha256:1')
      await user.type(within(dialog).getByLabelText(/Size/), '42')
      await user.type(within(dialog).getByLabelText(/Metadata/), '{{"k": 1}')
      await user.click(within(dialog).getByRole('button', { name: 'Add' }))
      await waitFor(() => expect(artifactsApi.create).toHaveBeenCalled())
      expect(vi.mocked(artifactsApi.create).mock.calls[0]).toEqual([
        'r1',
        { type: 'dataset', uri: 's3://b/x.csv', checksum: 'sha256:1', size_bytes: 42, metadata: { k: 1 } },
      ])
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
    })

    it('omits optional fields when empty', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.create).mockResolvedValue(art() as never)
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      const dialog = await screen.findByRole('dialog')
      await user.type(within(dialog).getByLabelText(/URI/), 'http://x/y')
      await user.click(within(dialog).getByRole('button', { name: 'Add' }))
      await waitFor(() => expect(artifactsApi.create).toHaveBeenCalled())
      expect(vi.mocked(artifactsApi.create).mock.calls[0][1]).toEqual({
        type: 'other', uri: 'http://x/y', checksum: undefined, size_bytes: undefined, metadata: undefined,
      })
    })

    it('rejects invalid metadata JSON without calling the API', async () => {
      const user = userEvent.setup()
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      const dialog = await screen.findByRole('dialog')
      await user.type(within(dialog).getByLabelText(/URI/), 'http://x/y')
      await user.type(within(dialog).getByLabelText(/Metadata/), 'not json')
      await user.click(within(dialog).getByRole('button', { name: 'Add' }))
      expect(await within(dialog).findByText('Metadata должен быть валидным JSON')).toBeInTheDocument()
      expect(artifactsApi.create).not.toHaveBeenCalled()
    })

    it('shows the server error inline and keeps the dialog open', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.create).mockRejectedValue(new Error('nope'))
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      const dialog = await screen.findByRole('dialog')
      await user.type(within(dialog).getByLabelText(/URI/), 'http://x/y')
      await user.click(within(dialog).getByRole('button', { name: 'Add' }))
      expect(await within(dialog).findByRole('alert')).toBeInTheDocument()
      expect(screen.getByRole('dialog')).toBeInTheDocument()
    })

    it('resets the form on cancel', async () => {
      const user = userEvent.setup()
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      let dialog = await screen.findByRole('dialog')
      await user.type(within(dialog).getByLabelText(/URI/), 'http://x/y')
      await user.click(within(dialog).getByRole('button', { name: 'Cancel' }))
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
      await user.click(screen.getByRole('button', { name: 'Add Artifact' }))
      dialog = await screen.findByRole('dialog')
      expect(within(dialog).getByLabelText(/URI/)).toHaveValue('')
    })
  })

  describe('row actions', () => {
    it('deletes after confirmation and can cancel', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.delete).mockResolvedValue(undefined)
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Delete' }))
      let dialog = await screen.findByRole('dialog')
      expect(within(dialog).getByText('s3://bucket/dir/model.bin')).toBeInTheDocument()
      await user.click(within(dialog).getByRole('button', { name: 'Cancel' }))
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
      expect(artifactsApi.delete).not.toHaveBeenCalled()

      await user.click(screen.getByRole('button', { name: 'Delete' }))
      dialog = await screen.findByRole('dialog')
      await user.click(within(dialog).getByRole('button', { name: 'Delete' }))
      await waitFor(() => expect(vi.mocked(artifactsApi.delete).mock.calls[0][0]).toBe('a1'))
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
    })

    it('approves only for owners and only unapproved artifacts', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.approve).mockResolvedValue(art() as never)
      vi.mocked(artifactsApi.list).mockResolvedValue({
        artifacts: [art(), art({ id: 'a2', uri: 'http://x/2', approved_by: 'bob' })],
        total: 2,
      } as never)
      renderPanel({ isOwner: true })
      await screen.findByText('model.bin')
      const approve = screen.getAllByLabelText('Approve')
      expect(approve).toHaveLength(1)
      await user.click(within(approve[0]).getByRole('button'))
      await waitFor(() => expect(vi.mocked(artifactsApi.approve).mock.calls[0][0]).toBe('a1'))
    })

    it('hides the approve button for non-owners', async () => {
      renderPanel()
      await screen.findByText('model.bin')
      expect(screen.queryByLabelText('Approve')).not.toBeInTheDocument()
    })
  })

  describe('download', () => {
    it('opens the presigned url for s3 artifacts', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.getDownloadUrl).mockResolvedValue({ download_url: 'https://signed/u', expires_in: 60 })
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(within(screen.getByLabelText('Download')).getByRole('button'))
      await waitFor(() => expect(openSpy).toHaveBeenCalledWith('https://signed/u', '_blank', 'noopener,noreferrer'))
    })

    it('falls back to the raw uri when the presign request fails', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.getDownloadUrl).mockRejectedValue(new Error('x'))
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(within(screen.getByLabelText('Download')).getByRole('button'))
      await waitFor(() => expect(openSpy).toHaveBeenCalledWith('s3://bucket/dir/model.bin', '_blank', 'noopener,noreferrer'))
    })

    it('opens non-remote uris directly without presigning', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.list).mockResolvedValue({ artifacts: [art({ uri: '/local/file.txt' })], total: 1 } as never)
      renderPanel()
      await screen.findByText('/local/file.txt')
      await user.click(within(screen.getByLabelText('Download')).getByRole('button'))
      expect(openSpy).toHaveBeenCalledWith('/local/file.txt', '_blank', 'noopener,noreferrer')
      expect(artifactsApi.getDownloadUrl).not.toHaveBeenCalled()
    })
  })

  describe('upload dialog', () => {
    class FakeXhr {
      static instances: FakeXhr[] = []
      static mode: 'ok' | 'http' | 'net' = 'ok'
      upload: { onprogress: ((e: unknown) => void) | null } = { onprogress: null }
      onload: (() => void) | null = null
      onerror: (() => void) | null = null
      status = 200
      headers: Record<string, string> = {}
      opened: [string, string] | null = null
      constructor() { FakeXhr.instances.push(this) }
      open(m: string, u: string) { this.opened = [m, u] }
      setRequestHeader(k: string, v: string) { this.headers[k] = v }
      send() {
        this.upload.onprogress?.({ lengthComputable: true, loaded: 1, total: 2 })
        this.upload.onprogress?.({ lengthComputable: false, loaded: 0, total: 0 })
        if (FakeXhr.mode === 'net') this.onerror?.()
        else {
          this.status = FakeXhr.mode === 'http' ? 500 : 200
          this.onload?.()
        }
      }
    }

    beforeEach(() => {
      FakeXhr.instances = []
      FakeXhr.mode = 'ok'
      vi.stubGlobal('XMLHttpRequest', FakeXhr)
      vi.mocked(artifactsApi.requestUploadUrl).mockResolvedValue({ upload_url: 'https://up/1', artifact_id: 'n', s3_key: 'k' })
    })

    async function openUpload(user: ReturnType<typeof userEvent.setup>, file?: File) {
      renderPanel()
      await screen.findByText('model.bin')
      await user.click(screen.getByRole('button', { name: 'Upload File' }))
      const dialog = await screen.findByRole('dialog')
      if (file) {
        const input = dialog.querySelector('input[type="file"]') as HTMLInputElement
        await user.upload(input, file)
      }
      return dialog
    }

    it('keeps Upload disabled until a file is chosen', async () => {
      const user = userEvent.setup()
      const dialog = await openUpload(user)
      expect(within(dialog).getByRole('button', { name: 'Upload' })).toBeDisabled()
    })

    it('uploads a file, shows success, then refreshes and closes', async () => {
      const user = userEvent.setup()
      const file = new File(['hello'], 'a.txt', { type: 'text/plain' })
      const dialog = await openUpload(user, file)
      expect(within(dialog).getByText(/a\.txt/)).toBeInTheDocument()
      expect(within(dialog).getByText(/5 B · text\/plain/)).toBeInTheDocument()
      await pickOption(user, within(dialog).getByRole('combobox'), 'log')
      await user.type(within(dialog).getByLabelText(/Metadata/), '{{"a": 1}')
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      await waitFor(() => expect(artifactsApi.requestUploadUrl).toHaveBeenCalled())
      expect(vi.mocked(artifactsApi.requestUploadUrl).mock.calls[0]).toEqual([
        'r1',
        { filename: 'a.txt', content_type: 'text/plain', type: 'log', size_bytes: 5, metadata: { a: 1 } },
      ])
      expect(await within(dialog).findByText('Загружено!')).toBeInTheDocument()
      expect(FakeXhr.instances[0].opened).toEqual(['PUT', 'https://up/1'])
      expect(FakeXhr.instances[0].headers['Content-Type']).toBe('text/plain')
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument(), { timeout: 3000 })
      expect(vi.mocked(artifactsApi.list).mock.calls.length).toBeGreaterThan(1)
    })

    it('uses a generic content type for unknown files', async () => {
      const user = userEvent.setup()
      const dialog = await openUpload(user, new File(['x'], 'blob.bin', { type: '' }))
      expect(within(dialog).getByText(/unknown type/)).toBeInTheDocument()
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      await waitFor(() => expect(artifactsApi.requestUploadUrl).toHaveBeenCalled())
      expect(vi.mocked(artifactsApi.requestUploadUrl).mock.calls[0][1]).toMatchObject({
        content_type: 'application/octet-stream', metadata: undefined,
      })
      await waitFor(() => expect(FakeXhr.instances[0].headers['Content-Type']).toBe('application/octet-stream'))
    })

    it('rejects invalid metadata before requesting a url', async () => {
      const user = userEvent.setup()
      const dialog = await openUpload(user, new File(['x'], 'a.txt', { type: 'text/plain' }))
      await user.type(within(dialog).getByLabelText(/Metadata/), 'oops')
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      expect(await within(dialog).findByText('Metadata должен быть валидным JSON')).toBeInTheDocument()
      expect(artifactsApi.requestUploadUrl).not.toHaveBeenCalled()
    })

    it('reports an HTTP failure of the storage PUT', async () => {
      const user = userEvent.setup()
      FakeXhr.mode = 'http'
      const dialog = await openUpload(user, new File(['x'], 'a.txt', { type: 'text/plain' }))
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      expect(await within(dialog).findByText('Ошибка загрузки файла: HTTP 500')).toBeInTheDocument()
    })

    it('reports a network failure of the storage PUT', async () => {
      const user = userEvent.setup()
      FakeXhr.mode = 'net'
      const dialog = await openUpload(user, new File(['x'], 'a.txt', { type: 'text/plain' }))
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      expect(await within(dialog).findByText('Ошибка сети при загрузке файла')).toBeInTheDocument()
    })

    it('reports a failure to get the upload url and can be cancelled', async () => {
      const user = userEvent.setup()
      vi.mocked(artifactsApi.requestUploadUrl).mockRejectedValue(new Error('denied'))
      const dialog = await openUpload(user, new File(['x'], 'a.txt', { type: 'text/plain' }))
      await user.click(within(dialog).getByRole('button', { name: 'Upload' }))
      expect(await within(dialog).findByRole('alert')).toBeInTheDocument()
      await user.click(within(dialog).getByRole('button', { name: 'Cancel' }))
      await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
    })
  })
})
