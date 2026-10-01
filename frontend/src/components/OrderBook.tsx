import type { OrderbookResponse } from '../types/api'

interface OrderBookProps {
  orderbook: OrderbookResponse | null
}

export function OrderBook({ orderbook }: OrderBookProps) {
  const asks = [...(orderbook?.asks ?? [])].sort((a, b) => a.price - b.price)
  const bids = [...(orderbook?.bids ?? [])].sort((a, b) => b.price - a.price)

  return (
    <section className="panel orderbook-panel">
      <div className="panel-header">
        <h2>Order book</h2>
        <span>Seq {orderbook?.sequenceNumber ?? '-'}</span>
      </div>
      <div className="book-table" aria-label="Live bid ask ladder">
        <div className="book-row book-head">
          <span>Side</span>
          <span>Price</span>
          <span>Qty</span>
        </div>
        <div className="book-side ask-side">
          {asks.length > 0 ? (
            asks.map((level) => (
              <div className="book-row ask" key={`ask-${level.price}`}>
                <span>ASK</span>
                <span>{level.price.toFixed(2)}</span>
                <span>{level.quantity}</span>
              </div>
            ))
          ) : (
            <div className="book-empty ask-empty">No ask levels</div>
          )}
        </div>
        <div className="spread">BID / ASK</div>
        <div className="book-side bid-side">
          {bids.length > 0 ? (
            bids.map((level) => (
              <div className="book-row bid" key={`bid-${level.price}`}>
                <span>BID</span>
                <span>{level.price.toFixed(2)}</span>
                <span>{level.quantity}</span>
              </div>
            ))
          ) : (
            <div className="book-empty bid-empty">No bid levels</div>
          )}
        </div>
      </div>
    </section>
  )
}
