import { describe, expect, test } from 'vitest'
import { decodeJwtPayload, decodeJwtUserId, isJwtExpired } from '../src/security'
import { makeJwt } from './helpers'

const nowSec = () => Math.floor(Date.now() / 1000)

describe('decodeJwtPayload', () => {
    test('decodes base64url payloads', () => {
        expect(decodeJwtPayload(makeJwt({ sub: 'u1' }))).toEqual({ sub: 'u1' })
    })

    test.each([
        ['a token with the wrong number of parts', 'a.b'],
        ['a payload that is not JSON', 'h.%%%.s'],
        ['a JSON null payload', `h.${Buffer.from('null').toString('base64url')}.s`],
        ['a JSON primitive payload', `h.${Buffer.from('42').toString('base64url')}.s`],
    ])('returns null for %s', (_name, token) => {
        expect(decodeJwtPayload(token)).toBeNull()
    })
})

describe('decodeJwtUserId', () => {
    test('prefers sub over user_id and rejects non-string ids', () => {
        expect(decodeJwtUserId(makeJwt({ sub: 'a', user_id: 'b' }))).toBe('a')
        expect(decodeJwtUserId(makeJwt({ user_id: 'b' }))).toBe('b')
        expect(decodeJwtUserId(makeJwt({ sub: 5 }))).toBeNull()
        expect(decodeJwtUserId('garbage')).toBeNull()
    })
})

describe('isJwtExpired', () => {
    test('honours numeric exp with a 30 second skew', () => {
        expect(isJwtExpired(makeJwt({ exp: nowSec() + 3600 }))).toBe(false)
        expect(isJwtExpired(makeJwt({ exp: nowSec() + 10 }))).toBe(true)
        expect(isJwtExpired(makeJwt({ exp: nowSec() + 10 }), 0)).toBe(false)
        expect(isJwtExpired(makeJwt({ exp: nowSec() - 1 }))).toBe(true)
    })

    test('accepts exp as a numeric string', () => {
        expect(isJwtExpired(makeJwt({ exp: String(nowSec() + 3600) }))).toBe(false)
        expect(isJwtExpired(makeJwt({ exp: String(nowSec() - 3600) }))).toBe(true)
    })

    test('accepts exp in milliseconds', () => {
        expect(isJwtExpired(makeJwt({ exp: (nowSec() + 3600) * 1000 }))).toBe(false)
        expect(isJwtExpired(makeJwt({ exp: (nowSec() - 3600) * 1000 }))).toBe(true)
    })

    test.each([
        ['a missing exp', {}],
        ['a non-numeric string exp', { exp: 'soon' }],
        ['an exp of another type', { exp: true }],
    ])('treats %s as not expired', (_name, claims) => {
        expect(isJwtExpired(makeJwt(claims))).toBe(false)
    })

    test('treats an undecodable token as not expired', () => {
        expect(isJwtExpired('garbage')).toBe(false)
    })
})
