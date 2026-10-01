interface ConnectionStatusProps {
  engineRunning: boolean | null
  connectionState: string
  lastSequenceNumber: number | null
  sequenceWarning: string | null
  error: string | null
}

export function ConnectionStatus({
  engineRunning,
  connectionState,
  lastSequenceNumber,
  sequenceWarning,
  error,
}: ConnectionStatusProps) {
  return (
    <section className="status-bar">
      <div>
        <span className={engineRunning ? 'dot ok' : 'dot warn'}></span>
        Engine {engineRunning ? 'running' : 'unknown'}
      </div>
      <div>
        <span className={connectionState === 'connected' ? 'dot ok' : 'dot warn'}></span>
        WebSocket {connectionState}
      </div>
      <div>Last seq {lastSequenceNumber ?? '-'}</div>
      {sequenceWarning && <div className="warning">{sequenceWarning}</div>}
      {error && <div className="error">{error}</div>}
    </section>
  )
}
