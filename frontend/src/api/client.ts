import type {
  ApiResponse,
  CancelOrderRequest,
  HealthResponse,
  ModifyOrderRequest,
  NewOrderRequest,
  OrderStatus,
  OrderbookResponse,
  TradeRecord,
} from '../types/api'

const configuredApiBaseUrl = import.meta.env.VITE_API_BASE_URL as string | undefined
const configuredWsUrl = import.meta.env.VITE_WS_URL as string | undefined
const API_BASE_URL = configuredApiBaseUrl === undefined ? 'http://127.0.0.1:18080' : configuredApiBaseUrl.replace(/\/$/, '')

function defaultWebSocketUrl() {
  if (API_BASE_URL.startsWith('http')) {
    return `${API_BASE_URL.replace(/^http/, 'ws')}/ws`
  }
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${protocol}://${window.location.host}/ws`
}

export const WS_URL = configuredWsUrl ? configuredWsUrl : defaultWebSocketUrl()

async function requestJson<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    ...init,
    headers: {
      'Content-Type': 'application/json',
      ...init?.headers,
    },
  })
  const body = (await response.json()) as T
  if (!response.ok) {
    const message = typeof body === 'object' && body && 'message' in body ? String(body.message) : response.statusText
    throw new Error(message)
  }
  return body
}

export function getHealth(): Promise<HealthResponse> {
  return requestJson<HealthResponse>('/health')
}

export function getOrderbook(): Promise<OrderbookResponse> {
  return requestJson<OrderbookResponse>('/orderbook')
}

export function getTrades(): Promise<TradeRecord[]> {
  return requestJson<TradeRecord[]>('/trades')
}

export function getOrders(): Promise<OrderStatus[]> {
  return requestJson<OrderStatus[]>('/orders')
}

export function submitOrder(order: NewOrderRequest): Promise<ApiResponse> {
  const body = order.orderType === 'MARKET' ? { ...order, price: undefined } : order
  return requestJson<ApiResponse>('/orders', {
    method: 'POST',
    body: JSON.stringify(body),
  })
}

export function cancelOrder(order: CancelOrderRequest): Promise<ApiResponse> {
  return requestJson<ApiResponse>('/orders', {
    method: 'DELETE',
    body: JSON.stringify(order),
  })
}

export function modifyOrder(order: ModifyOrderRequest): Promise<ApiResponse> {
  return requestJson<ApiResponse>('/orders', {
    method: 'PATCH',
    body: JSON.stringify(order),
  })
}
