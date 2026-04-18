import { useEffect, useRef, useState } from 'react'
import { notifyError } from '../utils/notify'

export interface CountdownState {
  remaining: number | null
  isWarning: boolean
  isExpired: boolean
}

/**
 * Tracks remaining milliseconds until `deadline`.
 * Returns null when deadline is null (feature disabled).
 * Fires a single notifyError when less than 5 minutes remain.
 */
export function useCountdown(deadline: number | null): CountdownState {
  const [remaining, setRemaining] = useState<number | null>(() =>
    deadline !== null ? deadline - Date.now() : null,
  )
  const warnedRef = useRef(false)

  useEffect(() => {
    if (deadline === null) {
      setRemaining(null)
      warnedRef.current = false
      return
    }

    const tick = () => {
      const r = deadline - Date.now()
      setRemaining(r)

      if (r > 0 && r < 5 * 60 * 1000 && !warnedRef.current) {
        warnedRef.current = true
        notifyError('Запуск завершится автоматически менее чем через 5 минут')
      }
    }

    tick()
    const id = setInterval(tick, 1000)
    return () => clearInterval(id)
  }, [deadline])

  const isWarning = remaining !== null && remaining > 0 && remaining < 5 * 60 * 1000
  const isExpired = remaining !== null && remaining <= 0

  return { remaining, isWarning, isExpired }
}
