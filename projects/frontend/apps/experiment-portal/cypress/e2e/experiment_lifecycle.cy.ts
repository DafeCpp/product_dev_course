/// <reference types="cypress" />

describe('experiment-portal happy path — full lifecycle', () => {
  const timestamp = Date.now()
  const projectName = `E2E Test Project ${timestamp}`
  const experimentName = `E2E Test Experiment ${timestamp}`

  it('logs in, creates project, creates experiment, creates run, views telemetry, logs out', () => {
    cy.viewport(1440, 900)
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

    // Confirm the project is persisted by the real backend.
    cy.reload()
    // Verify project appears in list
    cy.contains(projectName).should('be.visible')

    for (const width of [390, 1440]) {
      cy.viewport(width, 900)
      cy.contains(projectName).should('be.visible')
      cy.document().then((doc) => expect(doc.documentElement.scrollWidth).to.be.at.most(width))
      cy.screenshot(`backend-projects-${width}`, { capture: 'viewport', scale: true })
    }

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

    // Verify the experiment detail page is open
    cy.url().should('match', /\/experiments\/[0-9a-f-]+$/)

    // Create a run
    cy.createRun()

    // Creating a run opens its detail page; reload verifies server persistence.
    cy.url().should('match', /\/runs\/[0-9a-f-]+$/)
    cy.reload()
    cy.contains(/Run \d+/).should('be.visible')
    cy.screenshot('backend-run-detail', { capture: 'viewport', scale: true })

    // Navigate to telemetry viewer
    cy.get('a, button').contains(/telemetry|телеметрия/i).then(($el) => {
      if ($el.length) {
        cy.wrap($el).first().click()
      }
    })

    // Give time for telemetry page to load (may be complex with WebSocket/streaming)
    cy.url().should('include', '/telemetry', { timeout: 10000 })

    // Verify telemetry viewer is loaded (look for key elements)
    cy.contains(/телеметрия|сенсор|панели/i).should('be.visible')

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
