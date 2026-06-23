import type { Config } from 'jest'

const config: Config = {
    preset: 'ts-jest',
    testEnvironment: 'node',
    testMatch: ['<rootDir>/test/**/*.test.ts'],
    clearMocks: true,
    restoreMocks: true,
    verbose: false,
    coverageDirectory: 'coverage',
    coverageReporters: ['text', 'html', 'lcov'],
    collectCoverageFrom: [
        'src/**/*.ts',
        '!src/**/*.d.ts',
        '!src/**/__mocks__/**',
    ],
    // Ratchet floor — measured 60.6% lines / 51.2% branches at PR #100 baseline.
    // Plan target: 90% (aspirational); raise as wiring code in index.ts gets covered.
    coverageThreshold: {
        global: {
            lines: 58,
            statements: 58,
            functions: 55,
            branches: 48,
        },
    },
}

export default config

