/// <reference types="cypress" />

describe('experiment-portal happy path — full lifecycle', () => {
  const timestamp = Date.now()
  const projectName = `E2E Test Project ${timestamp}`
  const experimentName = `E2E Test Experiment ${timestamp}`

  it('logs in, creates project, creates experiment, creates run, views telemetry, logs out', () => {
    // Login
    cy.loginAsAdmin()

    // Verify we're on projects or experiments page (depends on redirect after login)
    cy.url().should('match', /\/(projects|experiments)/)

    // Navigate to projects if needed
    cy.get('a, button').contains(/projects|проекты/i).then(($el) => {
      if ($el.length) {
        cy.wrap($el).click()
      }
    })
    cy.url().should('include', '/projects')

    // Create project
    cy.createProject(projectName)

    // Verify project appears in list
    cy.contains(projectName).should('be.visible')

    // Open project
    cy.openProject(projectName)

    // Verify we're in experiments view
    cy.url().should('include', '/experiments')

    // Create experiment
    cy.createExperiment(experimentName)

    // Verify experiment appears
    cy.contains(experimentName).should('be.visible')

    // Open experiment to see runs
    cy.openExperiment(experimentName)

    // Verify we're in runs view
    cy.url().should('include', '/runs')

    // Create a run
    cy.createRun()

    // Verify run appears (might be in a list or table)
    cy.contains(/run|запуск|execution/i).should('be.visible')

    // Navigate to telemetry viewer
    cy.get('a, button').contains(/telemetry|телеметрия/i).then(($el) => {
      if ($el.length) {
        cy.wrap($el).first().click()
      }
    })

    // Give time for telemetry page to load (may be complex with WebSocket/streaming)
    cy.url().should('include', '/telemetry', { timeout: 10000 })

    // Verify telemetry viewer is loaded (look for key elements)
    cy.get('body').should('contain', /telemetry|chart|plot|data|sensor/i)

    // Logout
    cy.logout()

    // Verify we're back at login
    cy.url().should('include', '/login')
    cy.contains(/вход|login|sign in/i).should('be.visible')
  })

  it('login page is accessible', () => {
    cy.visit('/login')
    cy.contains(/вход в систему|login|sign in/i).should('be.visible')
  })
})
