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
    page?: number
    page_size?: number
  }): Promise<WebhooksListResponse> => {
    return await apiGet('/api/v1/webhooks', { params })
  },

  create: async (data: WebhookSubscriptionCreate): Promise<WebhookSubscription> => {
    return await apiPost('/api/v1/webhooks', data)
  },

  delete: async (webhookId: string): Promise<void> => {
    await apiDelete(`/api/v1/webhooks/${webhookId}`)
  },

  listDeliveries: async (params?: {
    status?: string
    page?: number
    page_size?: number
  }): Promise<WebhookDeliveriesListResponse> => {
    return await apiGet('/api/v1/webhooks/deliveries', { params })
  },

  retryDelivery: async (deliveryId: string): Promise<void> => {
    await apiPost(`/api/v1/webhooks/deliveries/${deliveryId}:retry`)
  },
}
