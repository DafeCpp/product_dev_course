/**
 * Format number with ru-RU locale (1 234,56 for 1234.56)
 */
export function formatNumber(value: number, fractionDigits = 2): string {
  if (!Number.isFinite(value)) {
    return '—'
  }
  return value.toLocaleString('ru-RU', {
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
  })
}

/**
 * Format bytes as human-readable size (e.g. "1.2 MB")
 */
export function formatBytes(bytes: number): string {
  if (bytes < 0 || !Number.isFinite(bytes)) {
    return '—'
  }

  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let size = bytes
  let unitIndex = 0

  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024
    unitIndex += 1
  }

  if (unitIndex === 0) {
    return `${Math.round(size)} ${units[unitIndex]}`
  }

  return `${size.toFixed(1)} ${units[unitIndex]}`
}

/**
 * Format frequency in Hz as human-readable (e.g. "1.5 kHz")
 */
export function formatHz(hz: number): string {
  if (hz < 0 || !Number.isFinite(hz)) {
    return '—'
  }

  if (hz < 1000) {
    return `${hz.toFixed(1)} Hz`
  }

  if (hz < 1_000_000) {
    return `${(hz / 1000).toFixed(1)} kHz`
  }

  return `${(hz / 1_000_000).toFixed(1)} MHz`
}
