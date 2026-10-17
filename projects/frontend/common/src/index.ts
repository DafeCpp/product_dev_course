export type {
  TelemetryStreamRecord,
  TelemetryQueryRecord,
  TelemetryQueryResponse,
  TelemetryAggregatedRecord,
  TelemetryAggregatedResponse,
  TelemetryStreamOpenParams,
  TelemetryStreamOpener,
  TelemetryQueryFn,
  TelemetryAggregatedFn,
} from './types/telemetry'

export { useTelemetryStream } from './hooks/useTelemetryStream'
export type {
  UseTelemetryStreamOptions,
  UseTelemetryStreamResult,
  TelemetryStreamStatus,
  TelemetryStreamCursor,
} from './hooks/useTelemetryStream'

export { useTelemetryQuery } from './hooks/useTelemetryQuery'
export type {
  UseTelemetryQueryParams,
  UseTelemetryQueryRawParams,
  UseTelemetryQueryAggregatedParams,
  UseTelemetryQueryResult,
} from './hooks/useTelemetryQuery'

export { computeBackoffDelayMs } from './hooks/backoff'
export type { TelemetryBackoffOptions } from './hooks/backoff'
