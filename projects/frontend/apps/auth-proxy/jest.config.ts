import type { Config } from 'jest'

const config: Config = {
    preset: 'ts-jest',
    testEnvironment: 'node',
    testMatch: ['<rootDir>/test/**/*.test.ts'],
    clearMocks: true,
    restoreMocks: true,
    verbose: false,
    coverageDirectory: 'coverage',
    coverageReporters: ['text', 'html', 'lcov', 'json-summary', 'cobertura'],
    collectCoverageFrom: [
        'src/**/*.ts',
        '!src/**/*.d.ts',
        '!src/**/__mocks__/**',
    ],
    // Initial ratchet from Coverage Baseline run 29654381568.
    coverageThreshold: process.env.COVERAGE_ENFORCE_RATCHET === 'true' ? {
        global: {
            lines: 69.52,
            statements: 58,
            functions: 55,
            branches: 59.55,
        },
    } : undefined,
}

export default config
