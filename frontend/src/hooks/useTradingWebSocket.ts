import { useEffect, useRef, useState } from 'react'
import { WS_URL } from '../api/client'
import type { WebSocketMessage } from '../types/api'

type ConnectionState = 'connecting' | 'connected' | 'reconnecting' | 'disconnected'

interface UseTradingWebSocketOptions {
  onMessage: (message: WebSocketMessage) => void
  onSequenceGap: () => void
}

export function useTradingWebSocket({ onMessage, onSequenceGap }: UseTradingWebSocketOptions) {
  const [connectionState, setConnectionState] = useState<ConnectionState>('connecting')
  const [lastSequenceNumber, setLastSequenceNumber] = useState<number | null>(null)
  const [sequenceWarning, setSequenceWarning] = useState<string | null>(null)
  const callbackRef = useRef({ onMessage, onSequenceGap })

  useEffect(() => {
    callbackRef.current = { onMessage, onSequenceGap }
  }, [onMessage, onSequenceGap])

  useEffect(() => {
    let socket: WebSocket | null = null
    let reconnectTimer: number | undefined
    let closedByHook = false
    let latestSequence: number | null = null
    let connectionId = 0

    const connect = (isReconnect = false) => {
      if (closedByHook) {
        return
      }

      const currentConnectionId = ++connectionId
      setConnectionState(isReconnect ? 'reconnecting' : 'connecting')

      const nextSocket = new WebSocket(WS_URL)
      socket = nextSocket

      const isCurrentSocket = () => !closedByHook && currentConnectionId === connectionId && socket === nextSocket

      nextSocket.onopen = () => {
        if (!isCurrentSocket()) {
          return
        }
        setConnectionState('connected')
        setSequenceWarning(null)
      }

      nextSocket.onmessage = (event) => {
        if (!isCurrentSocket()) {
          return
        }

        let message: WebSocketMessage
        try {
          message = JSON.parse(event.data) as WebSocketMessage
        } catch {
          return
        }

        if ('sequenceNumber' in message && typeof message.sequenceNumber === 'number') {
          const sequenceNumber = message.sequenceNumber
          if (latestSequence !== null && sequenceNumber > latestSequence + 1) {
            setSequenceWarning(`Sequence gap detected: expected ${latestSequence + 1}, received ${sequenceNumber}`)
            callbackRef.current.onSequenceGap()
          } else if (latestSequence !== null && sequenceNumber < latestSequence) {
            setSequenceWarning(`Out-of-order sequence ignored: received ${sequenceNumber} after ${latestSequence}`)
            return
          }
          latestSequence = Math.max(latestSequence ?? 0, sequenceNumber)
          setLastSequenceNumber(latestSequence)
        }

        callbackRef.current.onMessage(message)
      }

      nextSocket.onclose = () => {
        if (!isCurrentSocket()) {
          return
        }
        if (closedByHook) {
          setConnectionState('disconnected')
          return
        }
        setConnectionState('reconnecting')
        reconnectTimer = window.setTimeout(() => connect(true), 1000)
      }

      nextSocket.onerror = () => {
        if (isCurrentSocket()) {
          nextSocket.close()
        }
      }
    }

    connect()

    return () => {
      closedByHook = true
      connectionId += 1
      if (reconnectTimer !== undefined) {
        window.clearTimeout(reconnectTimer)
      }
      socket?.close()
    }
  }, [])

  return { connectionState, lastSequenceNumber, sequenceWarning }
}
