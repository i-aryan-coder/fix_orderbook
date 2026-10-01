import { useState } from 'react'
import type { FormEvent } from 'react'
import type { ApiResponse, NewOrderRequest, OrderType, Side } from '../types/api'

interface OrderEntryProps {
  onSubmit: (order: NewOrderRequest) => Promise<ApiResponse>
}

export function OrderEntry({ onSubmit }: OrderEntryProps) {
  const [side, setSide] = useState<Side>('BUY')
  const [orderType, setOrderType] = useState<OrderType>('LIMIT')
  const [price, setPrice] = useState('100')
  const [quantity, setQuantity] = useState('10')
  const [clientOrderId, setClientOrderId] = useState(() => `WEB-${Date.now()}`)
  const [message, setMessage] = useState('')
  const [submitting, setSubmitting] = useState(false)

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setSubmitting(true)
    setMessage('Submitting order to backend...')
    try {
      const response = await onSubmit({
        clientOrderId,
        symbol: 'STOCK',
        side,
        orderType,
        price: orderType === 'MARKET' ? undefined : Number(price),
        quantity: Number(quantity),
      })
      setMessage(`${response.message}: order ${response.orderId}`)
      setClientOrderId(`WEB-${Date.now()}`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Order rejected')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <section className="panel order-entry">
      <div className="panel-header">
        <h2>Order entry</h2>
        <span>REST command</span>
      </div>
      <form onSubmit={submit}>
        <div className="segmented">
          <button type="button" className={side === 'BUY' ? 'buy selected' : ''} onClick={() => setSide('BUY')}>
            BUY
          </button>
          <button type="button" className={side === 'SELL' ? 'sell selected' : ''} onClick={() => setSide('SELL')}>
            SELL
          </button>
        </div>

        <label>
          Type
          <select value={orderType} onChange={(event) => setOrderType(event.target.value as OrderType)}>
            <option value="LIMIT">Limit</option>
            <option value="MARKET">Market</option>
            <option value="FAK">FAK</option>
          </select>
        </label>

        <label>
          ClOrdID
          <input value={clientOrderId} onChange={(event) => setClientOrderId(event.target.value)} />
        </label>

        <label>
          Price
          <input
            disabled={orderType === 'MARKET'}
            min="1"
            type="number"
            value={orderType === 'MARKET' ? '' : price}
            onChange={(event) => setPrice(event.target.value)}
          />
        </label>

        <label>
          Quantity
          <input min="1" required type="number" value={quantity} onChange={(event) => setQuantity(event.target.value)} />
        </label>

        <button className={side === 'BUY' ? 'submit buy' : 'submit sell'} disabled={submitting} type="submit">
          {submitting ? 'Submitting...' : `Submit ${side}`}
        </button>
      </form>
      {message && <p className="status-message">{message}</p>}
    </section>
  )
}
