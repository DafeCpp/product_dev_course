/** A controllable fake SSE stream for testing consumers of a `Response`-shaped stream. */
export function createControllableSSEStream() {
  let controller!: ReadableStreamDefaultController<Uint8Array>
  const encoder = new TextEncoder()

  const stream = new ReadableStream<Uint8Array>({
    start(c) {
      controller = c
    },
  })

  return {
    stream,
    push(event: string, data: unknown) {
      const payload = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`
      controller.enqueue(encoder.encode(payload))
    },
    pushRaw(raw: string) {
      controller.enqueue(encoder.encode(raw))
    },
    close() {
      controller.close()
    },
    error(err: unknown) {
      controller.error(err)
    },
  }
}

export function fakeResponse(stream: ReadableStream<Uint8Array>): Response {
  return { body: stream } as unknown as Response
}
