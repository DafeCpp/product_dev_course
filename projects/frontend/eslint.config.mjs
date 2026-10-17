// Shared ESLint flat config for every frontend workspace under projects/frontend.
// ESLint looks it up from each linted file's directory, so `npm run lint`
// inside any app or `common` picks this file up.
import js from '@eslint/js'
import globals from 'globals'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  {
    ignores: [
      '**/node_modules/**',
      '**/dist/**',
      '**/dist-local/**',
      '**/coverage/**',
    ],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['**/*.{ts,tsx,mts,cts}'],
    rules: {
      '@typescript-eslint/no-unused-vars': [
        'error',
        {
          argsIgnorePattern: '^_',
          varsIgnorePattern: '^_',
          caughtErrorsIgnorePattern: '^_',
          ignoreRestSiblings: true,
        },
      ],
    },
  },
  // Browser apps and shared React code.
  {
    files: [
      'common/**/*.{ts,tsx}',
      'apps/experiment-portal/**/*.{ts,tsx}',
      'apps/sensor-simulator/**/*.{ts,tsx}',
    ],
    languageOptions: {
      globals: globals.browser,
    },
    plugins: {
      'react-hooks': reactHooks,
      'react-refresh': reactRefresh,
    },
    rules: {
      ...reactHooks.configs.recommended.rules,
      'react-refresh/only-export-components': ['warn', { allowConstantExport: true }],
    },
  },
  // Node services and build tooling.
  {
    files: ['apps/auth-proxy/**/*.ts', '**/*.config.{ts,mts,js,mjs}'],
    languageOptions: {
      globals: globals.node,
    },
  },
  // Test files may use loose typing for mocks and fixtures.
  {
    files: ['**/*.test.{ts,tsx}', '**/setupTests.ts', '**/test/**/*.ts'],
    languageOptions: {
      globals: { ...globals.node, ...globals.vitest },
    },
    rules: {
      '@typescript-eslint/no-explicit-any': 'off',
      'react-refresh/only-export-components': 'off',
    },
  },
)
