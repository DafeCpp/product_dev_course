export type SSEEvent = {
    event: string
    data: string
}

/**
 * Minimal SSE parser.
 * - Supports `event:` + one or multiple `data:` lines
 * - Ignores comment/heartbeat lines starting with `:`
 * - Splits events by blank line
 */
export function createSSEParser(onEvent: (evt: SSEEvent) => void) {
    let buffer = ''

    function feed(textChunk: string) {
        buffer += textChunk

        while (true) {
            const sepIdx = buffer.indexOf('\n\n')
            if (sepIdx === -1) break

            const rawBlock = buffer.slice(0, sepIdx)
            buffer = buffer.slice(sepIdx + 2)

            const lines = rawBlock.split('\n').map((l) => l.replace(/\r$/, ''))
            let eventName = 'message'
            const dataLines: string[] = []

            for (const line of lines) {
                if (!line) continue
                if (line.startsWith(':')) continue

                if (line.startsWith('event:')) {
                    eventName = line.slice('event:'.length).trim() || 'message'
                    continue
                }
                if (line.startsWith('data:')) {
                    dataLines.push(line.slice('data:'.length).trimStart())
                    continue
                }
            }

            if (dataLines.length === 0) continue
            onEvent({ event: eventName, data: dataLines.join('\n') })
        }
    }

    function reset() {
        buffer = ''
    }

    return { feed, reset }
}
