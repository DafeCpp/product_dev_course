import { z } from 'zod'

/** Validates a string as a JSON object and transforms it to Record<string, unknown>. */
const jsonObject = z
  .string()
  .superRefine((val, ctx) => {
    const trimmed = val.trim()
    if (!trimmed || trimmed === '{}') return
    try {
      const parsed = JSON.parse(trimmed)
      if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
        ctx.addIssue({
          code: z.ZodIssueCode.custom,
          message: 'Должен быть JSON-объект, например {}',
        })
      }
    } catch {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: 'Неверный формат JSON',
      })
    }
  })
  .transform((val): Record<string, unknown> => {
    const trimmed = val.trim()
    if (!trimmed) return {}
    try {
      return JSON.parse(trimmed) as Record<string, unknown>
    } catch {
      return {}
    }
  })

// ---------------------------------------------------------------------------
// Experiment
// ---------------------------------------------------------------------------

export const createExperimentSchema = z.object({
  project_id: z.string().min(1, 'Выберите проект'),
  name: z.string().min(1, 'Название обязательно').transform((s) => s.trim()),
  description: z.string().optional(),
  experiment_type: z.string().optional(),
  tagsInput: z
    .string()
    .transform((val) => val.split(',').map((t) => t.trim()).filter((t) => t.length > 0)),
  metadataInput: jsonObject,
})

export type CreateExperimentOutput = z.output<typeof createExperimentSchema>

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

export const createRunSchema = z.object({
  name: z.string().min(1, 'Название обязательно').transform((s) => s.trim()),
  notes: z.string().optional(),
  paramsJson: jsonObject,
  metadataJson: jsonObject,
})

export type CreateRunOutput = z.output<typeof createRunSchema>

// ---------------------------------------------------------------------------
// Sensor
// ---------------------------------------------------------------------------

export const createSensorSchema = z.object({
  project_id: z.string().min(1, 'Выберите хотя бы один проект'),
  name: z.string().min(1, 'Название датчика обязательно').transform((s) => s.trim()),
  type: z.string().min(1, 'Выберите тип датчика'),
  input_unit: z
    .string()
    .min(1, 'Укажите входную единицу измерения')
    .transform((s) => s.trim()),
  display_unit: z
    .string()
    .min(1, 'Укажите единицу отображения')
    .transform((s) => s.trim()),
  calibration_notes: z.string().optional(),
})

// ---------------------------------------------------------------------------
// Webhook
// ---------------------------------------------------------------------------

export const createWebhookSchema = z.object({
  target_url: z.string().min(1, 'URL обязателен').url('Введите корректный URL'),
  event_types: z
    .string()
    .superRefine((val, ctx) => {
      const types = val.split(',').map((s) => s.trim()).filter(Boolean)
      if (types.length === 0) {
        ctx.addIssue({
          code: z.ZodIssueCode.custom,
          message: 'Укажите хотя бы один тип события',
        })
      }
    })
    .transform((val) => val.split(',').map((s) => s.trim()).filter(Boolean)),
  secret: z.string().optional(),
})

export type CreateWebhookOutput = z.output<typeof createWebhookSchema>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Extract first error message per field from a ZodError. */
export function flatFieldErrors<T extends z.ZodTypeAny>(
  error: z.ZodError<z.input<T>>,
): Record<string, string | undefined> {
  const flat = error.flatten().fieldErrors as Record<string, string[] | undefined>
  return Object.fromEntries(
    Object.entries(flat).map(([k, v]) => [k, v?.[0]]),
  )
}
