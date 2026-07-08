/** Webhooks API */
import type {
  WebhookSubscription,
  WebhookSubscriptionCreate,
  WebhooksListResponse,
  WebhookDeliveriesListResponse,
} from '../types'
import { apiGet, apiPost, apiDelete } from './client'

export const webhooksApi = {
  list: async (params?: {
    project_id?: string
    page?: number
    page_size?: number
  }): Promise<WebhooksListResponse> => {
    return await apiGet('/api/v1/webhooks', { params })
  },

  create: async (
    data: WebhookSubscriptionCreate,
    params?: { project_id?: string }
  ): Promise<WebhookSubscription> => {
    return await apiPost('/api/v1/webhooks', data, { params })
  },

  delete: async (webhookId: string, params?: { project_id?: string }): Promise<void> => {
    await apiDelete(`/api/v1/webhooks/${webhookId}`, { params })
  },

  listDeliveries: async (params?: {
    project_id?: string
    status?: string
    page?: number
    page_size?: number
  }): Promise<WebhookDeliveriesListResponse> => {
    return await apiGet('/api/v1/webhooks/deliveries', { params })
  },

  retryDelivery: async (
    deliveryId: string,
    params?: { project_id?: string }
  ): Promise<void> => {
    await apiPost(`/api/v1/webhooks/deliveries/${deliveryId}:retry`, undefined, { params })
  },
}
