import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import {
  Alert,
  Box,
  Button,
  Chip,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
  FormControl,
  IconButton,
  InputLabel,
  Link,
  MenuItem,
  Select,
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TablePagination,
  TableRow,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material'
import type { SelectChangeEvent } from '@mui/material'
import { artifactsApi } from '../api/client'
import type { Artifact, ArtifactType } from '../types'
// Inline minimal SVG icons (no @mui/icons-material dependency needed)
function DeleteIcon() {
  return (
    <svg viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor">
      <path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zm2.46-7.12 1.41-1.41L12 12.59l2.12-2.12 1.41 1.41L13.41 14l2.12 2.12-1.41 1.41L12 15.41l-2.12 2.12-1.41-1.41L10.59 14l-2.13-2.12zM15.5 4l-1-1h-5l-1 1H5v2h14V4h-3.5z" />
    </svg>
  )
}

function CheckCircleOutlineIcon() {
  return (
    <svg viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor">
      <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8zm4.59-12.42L10 14.17l-2.59-2.58L6 13l4 4 8-8-1.41-1.42z" />
    </svg>
  )
}

function AddIcon() {
  return (
    <svg viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor">
      <path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z" />
    </svg>
  )
}

function UploadIcon() {
  return (
    <svg viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor">
      <path d="M9 16h6v-6h4l-7-7-7 7h4v6zm-4 2h14v2H5v-2z" />
    </svg>
  )
}

function DownloadIcon() {
  return (
    <svg viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor">
      <path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z" />
    </svg>
  )
}

// ── Helpers ──────────────────────────────────────────────────────────────────

const ARTIFACT_TYPES: ArtifactType[] = ['model', 'dataset', 'plot', 'log', 'config', 'other']

function humanBytes(bytes: number | null | undefined): string {
  if (bytes === null || bytes === undefined) return '—'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`
}

function typeColor(
  type: string
): 'default' | 'primary' | 'secondary' | 'success' | 'warning' | 'info' | 'error' {
  const map: Record<string, 'default' | 'primary' | 'secondary' | 'success' | 'warning' | 'info' | 'error'> = {
    model: 'primary',
    dataset: 'info',
    plot: 'success',
    log: 'default',
    config: 'warning',
    other: 'secondary',
  }
  return map[type] ?? 'default'
}

const PAGE_SIZE = 10

// ── Add Artifact Dialog ───────────────────────────────────────────────────────

interface AddDialogProps {
  open: boolean
  onClose: () => void
  onSubmit: (data: { type: string; uri: string; checksum: string; size_bytes: string; metadata: string }) => void
  loading: boolean
  error: string | null
}

function AddArtifactDialog({ open, onClose, onSubmit, loading, error }: AddDialogProps) {
  const [type, setType] = useState<string>('other')
  const [uri, setUri] = useState('')
  const [checksum, setChecksum] = useState('')
  const [sizeBytes, setSizeBytes] = useState('')
  const [metadata, setMetadata] = useState('')

  function handleClose() {
    setType('other')
    setUri('')
    setChecksum('')
    setSizeBytes('')
    setMetadata('')
    onClose()
  }

  function handleSubmit() {
    onSubmit({ type, uri, checksum, size_bytes: sizeBytes, metadata })
  }

  return (
    <Dialog open={open} onClose={handleClose} maxWidth="sm" fullWidth>
      <DialogTitle>Add Artifact</DialogTitle>
      <DialogContent sx={{ display: 'flex', flexDirection: 'column', gap: 2, pt: 2 }}>
        {error && <Alert severity="error">{error}</Alert>}

        <FormControl size="small" required>
          <InputLabel>Type</InputLabel>
          <Select
            value={type}
            label="Type"
            onChange={(e: SelectChangeEvent) => setType(e.target.value)}
          >
            {ARTIFACT_TYPES.map((t) => (
              <MenuItem key={t} value={t}>
                {t}
              </MenuItem>
            ))}
          </Select>
        </FormControl>

        <TextField
          label="URI"
          size="small"
          required
          value={uri}
          onChange={(e) => setUri(e.target.value)}
          placeholder="s3://bucket/path/to/artifact"
        />

        <TextField
          label="Checksum (optional)"
          size="small"
          value={checksum}
          onChange={(e) => setChecksum(e.target.value)}
          placeholder="sha256:..."
        />

        <TextField
          label="Size (bytes, optional)"
          size="small"
          type="number"
          value={sizeBytes}
          onChange={(e) => setSizeBytes(e.target.value)}
          inputProps={{ min: 0 }}
        />

        <TextField
          label="Metadata (JSON, optional)"
          size="small"
          multiline
          rows={3}
          value={metadata}
          onChange={(e) => setMetadata(e.target.value)}
          placeholder='{"key": "value"}'
        />
      </DialogContent>
      <DialogActions>
        <Button onClick={handleClose} disabled={loading}>
          Cancel
        </Button>
        <Button
          variant="contained"
          onClick={handleSubmit}
          disabled={!uri.trim() || loading}
          startIcon={loading ? <CircularProgress size={16} color="inherit" /> : undefined}
        >
          Add
        </Button>
      </DialogActions>
    </Dialog>
  )
}

// ── Upload Artifact Dialog ────────────────────────────────────────────────────

interface UploadDialogProps {
  open: boolean
  onClose: () => void
  runId: string
  projectId: string
  onSuccess: () => void
}

function UploadArtifactDialog({ open, onClose, runId, projectId: _projectId, onSuccess }: UploadDialogProps) {
  const [type, setType] = useState<string>('other')
  const [file, setFile] = useState<File | null>(null)
  const [metadata, setMetadata] = useState('')
  const [status, setStatus] = useState<'idle' | 'uploading' | 'done' | 'error'>('idle')
  const [progress, setProgress] = useState(0)
  const [errorMsg, setErrorMsg] = useState<string | null>(null)

  function handleClose() {
    if (status === 'uploading') return
    setType('other')
    setFile(null)
    setMetadata('')
    setStatus('idle')
    setProgress(0)
    setErrorMsg(null)
    onClose()
  }

  async function handleUpload() {
    if (!file) return
    setStatus('uploading')
    setProgress(0)
    setErrorMsg(null)
    try {
      let parsedMeta: Record<string, unknown> | undefined
      if (metadata.trim()) {
        try {
          parsedMeta = JSON.parse(metadata)
        } catch {
          setErrorMsg('Metadata должен быть валидным JSON')
          setStatus('error')
          return
        }
      }

      // Step 1: request presigned upload URL
      const { upload_url } = await artifactsApi.requestUploadUrl(runId, {
        filename: file.name,
        content_type: file.type || 'application/octet-stream',
        type,
        size_bytes: file.size,
        metadata: parsedMeta,
      })

      // Step 2: upload file directly to MinIO via PUT
      setProgress(30)
      const xhr = new XMLHttpRequest()
      await new Promise<void>((resolve, reject) => {
        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable) {
            setProgress(30 + Math.round((e.loaded / e.total) * 65))
          }
        }
        xhr.onload = () => {
          if (xhr.status >= 200 && xhr.status < 300) resolve()
          else reject(new Error(`Upload failed: HTTP ${xhr.status}`))
        }
        xhr.onerror = () => reject(new Error('Network error during upload'))
        xhr.open('PUT', upload_url)
        xhr.setRequestHeader('Content-Type', file.type || 'application/octet-stream')
        xhr.send(file)
      })

      setProgress(100)
      setStatus('done')
      setTimeout(() => {
        onSuccess()
        handleClose()
      }, 800)
    } catch (e: unknown) {
      setErrorMsg(e instanceof Error ? e.message : 'Upload failed')
      setStatus('error')
    }
  }

  return (
    <Dialog open={open} onClose={handleClose} maxWidth="sm" fullWidth>
      <DialogTitle>Upload Artifact</DialogTitle>
      <DialogContent sx={{ display: 'flex', flexDirection: 'column', gap: 2, pt: 2 }}>
        {errorMsg && <Alert severity="error">{errorMsg}</Alert>}
        {status === 'done' && <Alert severity="success">Загружено!</Alert>}

        <FormControl size="small" required>
          <InputLabel>Type</InputLabel>
          <Select
            value={type}
            label="Type"
            onChange={(e: SelectChangeEvent) => setType(e.target.value)}
            disabled={status === 'uploading'}
          >
            {ARTIFACT_TYPES.map((t) => (
              <MenuItem key={t} value={t}>{t}</MenuItem>
            ))}
          </Select>
        </FormControl>

        <Button
          variant="outlined"
          component="label"
          startIcon={<UploadIcon />}
          disabled={status === 'uploading'}
        >
          {file ? file.name : 'Выбрать файл'}
          <input
            type="file"
            hidden
            onChange={(e) => setFile(e.target.files?.[0] ?? null)}
          />
        </Button>

        {file && (
          <Typography variant="caption" color="text.secondary">
            {humanBytes(file.size)} · {file.type || 'unknown type'}
          </Typography>
        )}

        <TextField
          label="Metadata (JSON, optional)"
          size="small"
          multiline
          rows={2}
          value={metadata}
          onChange={(e) => setMetadata(e.target.value)}
          placeholder='{"key": "value"}'
          disabled={status === 'uploading'}
        />

        {status === 'uploading' && (
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <CircularProgress size={16} />
            <Typography variant="caption">{progress}%</Typography>
          </Box>
        )}
      </DialogContent>
      <DialogActions>
        <Button onClick={handleClose} disabled={status === 'uploading'}>
          Cancel
        </Button>
        <Button
          variant="contained"
          onClick={handleUpload}
          disabled={!file || status === 'uploading' || status === 'done'}
          startIcon={status === 'uploading' ? <CircularProgress size={16} color="inherit" /> : <UploadIcon />}
        >
          Upload
        </Button>
      </DialogActions>
    </Dialog>
  )
}

// ── Delete confirm dialog ─────────────────────────────────────────────────────

function DeleteDialog({
  artifact,
  onClose,
  onConfirm,
  loading,
}: {
  artifact: Artifact | null
  onClose: () => void
  onConfirm: () => void
  loading: boolean
}) {
  return (
    <Dialog open={!!artifact} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>Delete Artifact</DialogTitle>
      <DialogContent>
        <DialogContentText>
          Удалить артефакт <strong>{artifact?.uri}</strong>? Это действие необратимо.
        </DialogContentText>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose} disabled={loading}>
          Cancel
        </Button>
        <Button
          color="error"
          variant="contained"
          onClick={onConfirm}
          disabled={loading}
          startIcon={loading ? <CircularProgress size={16} color="inherit" /> : undefined}
        >
          Delete
        </Button>
      </DialogActions>
    </Dialog>
  )
}

// ── Main component ────────────────────────────────────────────────────────────

interface Props {
  runId: string
  projectId: string
  isOwner?: boolean
}

export default function ArtifactsPanel({ runId, projectId, isOwner = false }: Props) {
  const queryClient = useQueryClient()

  const [typeFilter, setTypeFilter] = useState<string>('')
  const [page, setPage] = useState(0)
  const [showAdd, setShowAdd] = useState(false)
  const [addError, setAddError] = useState<string | null>(null)
  const [deleteTarget, setDeleteTarget] = useState<Artifact | null>(null)
  const [showUpload, setShowUpload] = useState(false)
  const [downloadingId, setDownloadingId] = useState<string | null>(null)

  const { data, isLoading, error } = useQuery({
    queryKey: ['artifacts', runId, typeFilter, page],
    queryFn: () =>
      artifactsApi.list(runId, {
        type: typeFilter || undefined,
        limit: PAGE_SIZE,
        offset: page * PAGE_SIZE,
      }),
    enabled: !!runId,
  })

  const artifacts = data?.artifacts ?? []
  const total = data?.total ?? 0

  const createMutation = useMutation({
    mutationFn: (body: { type: string; uri: string; checksum?: string; size_bytes?: number; metadata?: Record<string, any> }) =>
      artifactsApi.create(runId, body),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['artifacts', runId] })
      setShowAdd(false)
      setAddError(null)
    },
    onError: (err: any) => {
      setAddError(err?.response?.data?.error || err?.message || 'Ошибка создания артефакта')
    },
  })

  const deleteMutation = useMutation({
    mutationFn: (artifactId: string) => artifactsApi.delete(artifactId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['artifacts', runId] })
      setDeleteTarget(null)
    },
  })

  const approveMutation = useMutation({
    mutationFn: (artifactId: string) => artifactsApi.approve(artifactId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['artifacts', runId] })
    },
  })

  function handleAddSubmit(formData: {
    type: string
    uri: string
    checksum: string
    size_bytes: string
    metadata: string
  }) {
    setAddError(null)
    let parsedMeta: Record<string, any> | undefined
    if (formData.metadata.trim()) {
      try {
        parsedMeta = JSON.parse(formData.metadata)
      } catch {
        setAddError('Metadata должен быть валидным JSON')
        return
      }
    }
    createMutation.mutate({
      type: formData.type,
      uri: formData.uri,
      checksum: formData.checksum || undefined,
      size_bytes: formData.size_bytes ? Number(formData.size_bytes) : undefined,
      metadata: parsedMeta,
    })
  }

  async function handleDownload(artifact: Artifact) {
    if (!artifact.uri.startsWith('s3://') && !artifact.uri.startsWith('http')) {
      window.open(artifact.uri, '_blank', 'noopener,noreferrer')
      return
    }
    setDownloadingId(artifact.id)
    try {
      const { download_url } = await artifactsApi.getDownloadUrl(artifact.id)
      window.open(download_url, '_blank', 'noopener,noreferrer')
    } catch {
      // fallback: open URI directly
      window.open(artifact.uri, '_blank', 'noopener,noreferrer')
    } finally {
      setDownloadingId(null)
    }
  }

  return (
    <Box>
      {/* Toolbar */}
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 2, mb: 2, flexWrap: 'wrap' }}>
        <Typography variant="subtitle1" fontWeight={600} sx={{ flex: 1 }}>
          Artifacts
        </Typography>

        <FormControl size="small" sx={{ minWidth: 140 }}>
          <InputLabel>Filter by type</InputLabel>
          <Select
            value={typeFilter}
            label="Filter by type"
            onChange={(e: SelectChangeEvent) => {
              setTypeFilter(e.target.value)
              setPage(0)
            }}
          >
            <MenuItem value="">All</MenuItem>
            {ARTIFACT_TYPES.map((t) => (
              <MenuItem key={t} value={t}>
                {t}
              </MenuItem>
            ))}
          </Select>
        </FormControl>

        <Button
          variant="contained"
          size="small"
          startIcon={<AddIcon />}
          onClick={() => {
            setAddError(null)
            setShowAdd(true)
          }}
        >
          Add Artifact
        </Button>
        <Button
          variant="outlined"
          size="small"
          startIcon={<UploadIcon />}
          onClick={() => setShowUpload(true)}
        >
          Upload File
        </Button>
      </Box>

      {/* Content */}
      {isLoading ? (
        <Box sx={{ display: 'flex', justifyContent: 'center', py: 4 }}>
          <CircularProgress />
        </Box>
      ) : error ? (
        <Alert severity="error">Не удалось загрузить артефакты</Alert>
      ) : artifacts.length === 0 ? (
        <Typography color="text.secondary" sx={{ py: 3, textAlign: 'center' }}>
          {typeFilter ? `Нет артефактов типа "${typeFilter}"` : 'Нет артефактов'}
        </Typography>
      ) : (
        <>
          <TableContainer>
            <Table size="small">
              <TableHead>
                <TableRow>
                  <TableCell>Type</TableCell>
                  <TableCell>URI</TableCell>
                  <TableCell align="right">Size</TableCell>
                  <TableCell>Checksum</TableCell>
                  <TableCell>Approved by</TableCell>
                  <TableCell>Created</TableCell>
                  <TableCell align="right">Actions</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                {artifacts.map((artifact) => (
                  <TableRow key={artifact.id} hover>
                    <TableCell>
                      <Chip label={artifact.type} size="small" color={typeColor(artifact.type)} />
                    </TableCell>
                    <TableCell sx={{ maxWidth: 260, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                      {artifact.uri.startsWith('s3://') ? (
                        <Tooltip title={artifact.uri}>
                          <span style={{ fontFamily: 'monospace', fontSize: '0.75rem', color: '#64748b' }}>
                            {artifact.uri.split('/').pop() ?? artifact.uri}
                          </span>
                        </Tooltip>
                      ) : (
                        <Link href={artifact.uri} target="_blank" rel="noopener noreferrer" underline="hover">
                          {artifact.uri}
                        </Link>
                      )}
                    </TableCell>
                    <TableCell align="right">
                      <Typography variant="body2">{humanBytes(artifact.size_bytes)}</Typography>
                    </TableCell>
                    <TableCell>
                      <Typography
                        variant="body2"
                        sx={{ fontFamily: 'monospace', fontSize: '0.75rem', maxWidth: 120, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}
                        title={artifact.checksum ?? undefined}
                      >
                        {artifact.checksum ?? '—'}
                      </Typography>
                    </TableCell>
                    <TableCell>
                      <Typography variant="body2">{artifact.approved_by ?? '—'}</Typography>
                    </TableCell>
                    <TableCell>
                      <Typography variant="body2" sx={{ whiteSpace: 'nowrap' }}>
                        {new Date(artifact.created_at).toLocaleString()}
                      </Typography>
                    </TableCell>
                    <TableCell align="right">
                      <Box sx={{ display: 'flex', justifyContent: 'flex-end', gap: 0.5 }}>
                        <Tooltip title="Download">
                          <span>
                            <IconButton
                              size="small"
                              onClick={() => handleDownload(artifact)}
                              disabled={downloadingId === artifact.id}
                            >
                              {downloadingId === artifact.id
                                ? <CircularProgress size={16} />
                                : <DownloadIcon />
                              }
                            </IconButton>
                          </span>
                        </Tooltip>
                        {isOwner && !artifact.approved_by && (
                          <Tooltip title="Approve">
                            <span>
                              <IconButton
                                size="small"
                                color="success"
                                onClick={() => approveMutation.mutate(artifact.id)}
                                disabled={approveMutation.isPending}
                              >
                                <CheckCircleOutlineIcon />
                              </IconButton>
                            </span>
                          </Tooltip>
                        )}
                        <Tooltip title="Delete">
                          <IconButton
                            size="small"
                            color="error"
                            onClick={() => setDeleteTarget(artifact)}
                          >
                            <DeleteIcon />
                          </IconButton>
                        </Tooltip>
                      </Box>
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </TableContainer>
          <TablePagination
            component="div"
            count={total}
            page={page}
            rowsPerPage={PAGE_SIZE}
            rowsPerPageOptions={[PAGE_SIZE]}
            onPageChange={(_, newPage) => setPage(newPage)}
          />
        </>
      )}

      {/* Dialogs */}
      <AddArtifactDialog
        open={showAdd}
        onClose={() => {
          setShowAdd(false)
          setAddError(null)
        }}
        onSubmit={handleAddSubmit}
        loading={createMutation.isPending}
        error={addError}
      />

      <DeleteDialog
        artifact={deleteTarget}
        onClose={() => setDeleteTarget(null)}
        onConfirm={() => deleteTarget && deleteMutation.mutate(deleteTarget.id)}
        loading={deleteMutation.isPending}
      />

      <UploadArtifactDialog
        open={showUpload}
        onClose={() => setShowUpload(false)}
        runId={runId}
        projectId={projectId}
        onSuccess={() => {
          queryClient.invalidateQueries({ queryKey: ['artifacts', runId] })
          setShowUpload(false)
        }}
      />
    </Box>
  )
}
