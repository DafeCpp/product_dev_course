import type { Config } from 'jest'

const config: Config = {
    preset: 'ts-jest',
    testEnvironment: 'node',
    testMatch: ['<rootDir>/test/**/*.test.ts'],
    clearMocks: true,
    restoreMocks: true,
    verbose: false,
}

export default config

