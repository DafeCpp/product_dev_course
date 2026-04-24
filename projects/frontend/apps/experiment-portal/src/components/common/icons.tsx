import type { SVGProps } from 'react'

type IconProps = SVGProps<SVGSVGElement>

export function SearchIcon({ width = 15, height = 15, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <circle cx="9" cy="9" r="5.5" stroke="currentColor" strokeWidth="1.6" />
      <path d="m13.5 13.5 3 3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
    </svg>
  )
}

export function UserIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <circle cx="10" cy="7" r="3" stroke="currentColor" strokeWidth="1.5" />
      <path d="M4 16.5c.9-2.6 3.2-4 6-4s5.1 1.4 6 4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" />
    </svg>
  )
}

export function EyeIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <path d="M2 12s3-7 10-7 10 7 10 7-3 7-10 7S2 12 2 12z" />
      <circle cx="12" cy="12" r="3" />
    </svg>
  )
}

export function TeamIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <circle cx="9" cy="8" r="3" />
      <path d="M2 21a7 7 0 0 1 14 0" />
      <circle cx="17" cy="7" r="2.5" />
      <path d="M22 18a5 5 0 0 0-8-3.5" />
    </svg>
  )
}

export function FolderIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <path
        d="M2.5 6.5A1.5 1.5 0 0 1 4 5h3.3a1.5 1.5 0 0 1 1.06.44l.94.94a1.5 1.5 0 0 0 1.06.44H16a1.5 1.5 0 0 1 1.5 1.5v6A1.5 1.5 0 0 1 16 15.82H4A1.5 1.5 0 0 1 2.5 14.3V6.5Z"
        stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round"
      />
    </svg>
  )
}

export function TagIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <path
        d="M10.5 3h5v5l-6.5 6.5a1.5 1.5 0 0 1-2.1 0l-2.9-2.9a1.5 1.5 0 0 1 0-2.1L10.5 3Z"
        stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round"
      />
      <circle cx="12.5" cy="6.5" r=".9" fill="currentColor" />
    </svg>
  )
}

export function SensorIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <circle cx="10" cy="10" r="2" fill="currentColor" />
      <circle cx="10" cy="10" r="5" stroke="currentColor" strokeWidth="1.4" />
      <circle cx="10" cy="10" r="8" stroke="currentColor" strokeWidth="1.2" opacity=".5" />
    </svg>
  )
}

export function DeleteIcon({ width = '1em', height = '1em', ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="currentColor" {...props}>
      <path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zm2.46-7.12 1.41-1.41L12 12.59l2.12-2.12 1.41 1.41L13.41 14l2.12 2.12-1.41 1.41L12 15.41l-2.12 2.12-1.41-1.41L10.59 14l-2.13-2.12zM15.5 4l-1-1h-5l-1 1H5v2h14V4h-3.5z" />
    </svg>
  )
}

export function CheckCircleOutlineIcon({ width = '1em', height = '1em', ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="currentColor" {...props}>
      <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8zm4.59-12.42L10 14.17l-2.59-2.58L6 13l4 4 8-8-1.41-1.42z" />
    </svg>
  )
}

export function AddIcon({ width = '1em', height = '1em', ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="currentColor" {...props}>
      <path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z" />
    </svg>
  )
}

export function UploadIcon({ width = '1em', height = '1em', ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="currentColor" {...props}>
      <path d="M9 16h6v-6h4l-7-7-7 7h4v6zm-4 2h14v2H5v-2z" />
    </svg>
  )
}

export function DownloadIcon({ width = '1em', height = '1em', ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="currentColor" {...props}>
      <path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z" />
    </svg>
  )
}

export function ListSearchIcon({ width = 15, height = 15, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <path d="M3 6h14M3 10h9M3 14h6" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
    </svg>
  )
}

export function GridIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <rect x="3" y="3" width="6" height="6" rx="1" stroke="currentColor" strokeWidth="1.4" />
      <rect x="11" y="3" width="6" height="6" rx="1" stroke="currentColor" strokeWidth="1.4" />
      <rect x="3" y="11" width="6" height="6" rx="1" stroke="currentColor" strokeWidth="1.4" />
      <rect x="11" y="11" width="6" height="6" rx="1" stroke="currentColor" strokeWidth="1.4" />
    </svg>
  )
}

export function LockIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <rect x="4" y="9" width="12" height="8" rx="2" stroke="currentColor" strokeWidth="1.5" />
      <path d="M7 9V6.5a3 3 0 0 1 6 0V9" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  )
}

export function FlaskIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <path d="M8 2.5h4M8.5 2.5v5.2L4.4 14.3a1.6 1.6 0 0 0 1.36 2.42h8.48a1.6 1.6 0 0 0 1.36-2.42L11.5 7.7V2.5" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      <path d="M6.3 11.2h7.4" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" />
    </svg>
  )
}

export function PlayCircleIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 20 20" fill="none" {...props}>
      <circle cx="10" cy="10" r="7.25" stroke="currentColor" strokeWidth="1.5" />
      <path d="M8.4 7.6v4.8l4-2.4-4-2.4Z" fill="currentColor" />
    </svg>
  )
}

export function StopCircleIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <circle cx="12" cy="12" r="10" />
      <rect x="9" y="9" width="6" height="6" rx="1" fill="currentColor" stroke="none" />
    </svg>
  )
}

export function XIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <path d="M18 6 6 18" />
      <path d="m6 6 12 12" />
    </svg>
  )
}

export function RefreshCwIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <path d="M21 12a9 9 0 1 1-9-9" />
      <path d="M21 3v6h-6" />
    </svg>
  )
}

export function ArrowRightIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <path d="M5 12h14" />
      <path d="m13 6 6 6-6 6" />
    </svg>
  )
}

export function ExportIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <path d="M12 3v12" />
      <path d="m7 10 5 5 5-5" />
      <path d="M5 21h14" />
    </svg>
  )
}

export function SettingsIcon({ width = 14, height = 14, ...props }: IconProps) {
  return (
    <svg width={width} height={height} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      <circle cx="12" cy="12" r="3" />
      <path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1A1.7 1.7 0 0 0 4.6 9a1.7 1.7 0 0 0-.3-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z" />
    </svg>
  )
}
