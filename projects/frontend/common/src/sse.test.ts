import { describe, expect, it, vi } from 'vitest'
import { createSSEParser } from './sse'

describe('createSSEParser', () => {
  it('parses multiline events across chunks and ignores SSE comments', () => {
    const onEvent = vi.fn()
    const parser = createSSEParser(onEvent)

    parser.feed(': heartbeat\r\nevent: telemetry\r\ndata: first\r\ndata: second')
    expect(onEvent).not.toHaveBeenCalled()

    parser.feed('\n\n')
    expect(onEvent).toHaveBeenCalledWith({ event: 'telemetry', data: 'first\nsecond' })
  })

  it('uses message for unnamed or empty event names and skips data-less blocks', () => {
    const onEvent = vi.fn()
    const parser = createSSEParser(onEvent)

    parser.feed('event: ignored\n\n: keepalive\n\nevent:   \ndata: payload\n\ndata: next\n\n')

    expect(onEvent).toHaveBeenCalledTimes(2)
    expect(onEvent).toHaveBeenNthCalledWith(1, { event: 'message', data: 'payload' })
    expect(onEvent).toHaveBeenNthCalledWith(2, { event: 'message', data: 'next' })
  })

  it('discards an incomplete event when reset', () => {
    const onEvent = vi.fn()
    const parser = createSSEParser(onEvent)

    parser.feed('data: stale')
    parser.reset()
    parser.feed('data: fresh\n\n')

    expect(onEvent).toHaveBeenCalledWith({ event: 'message', data: 'fresh' })
  })

  it('ignores blank and unsupported fields within an event block', () => {
    const onEvent = vi.fn()
    const parser = createSSEParser(onEvent)

    parser.feed('event: telemetry\n\r\nid: 42\ndata: payload\n\n')

    expect(onEvent).toHaveBeenCalledWith({ event: 'telemetry', data: 'payload' })
  })
})
