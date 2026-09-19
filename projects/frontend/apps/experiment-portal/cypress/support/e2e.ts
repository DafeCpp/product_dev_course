/// <reference types="cypress" />

Cypress.Commands.add('loginAsAdmin', () => {
  cy.visit('/login')
  cy.get('input[type="text"]').type('admin')
  cy.get('input[type="password"]').type('Admin123')
  cy.contains('button', /вход|войти/i).click()
  cy.url().should('match', /\/(projects|experiments)/)
})

Cypress.Commands.add('createProject', (projectName: string) => {
  cy.get('button[aria-label="Создать проект"]').click()
  cy.get('#project_modal_name').type(projectName)
  cy.contains('button', /создать проект|save|create/i).click()
  cy.contains(projectName).should('be.visible')
})

Cypress.Commands.add('openProject', (projectName: string) => {
  cy.contains('.project-card', projectName).within(() => {
    cy.get('button[aria-label="Открыть проект"]').click()
  })
  cy.get('#project_modal_name').should('have.value', projectName)
  cy.contains('.modal-content button', 'Закрыть').click()
  cy.visit('/experiments')
  cy.get('#experiment_project_id').click()
  cy.contains('[role="option"]', projectName).click()
  cy.url().should('include', '/experiments')
})

Cypress.Commands.add('createExperiment', (experimentName: string) => {
  cy.get('button[aria-label="Создать эксперимент"]').click()
  cy.get('#experiment_name').type(experimentName)
  cy.contains('button', /создать эксперимент|create/i).click()
  cy.url().should('match', /\/experiments\/[0-9a-f-]+$/)
  cy.contains(experimentName).should('be.visible')
})

Cypress.Commands.add('openExperiment', (experimentName: string) => {
  cy.visit('/experiments')
  cy.contains('a.experiment-card', experimentName).click()
  cy.url().should('match', /\/experiments\/[0-9a-f-]+$/)
})

Cypress.Commands.add('createRun', () => {
  const runName = `Run ${Date.now()}`
  cy.contains('button', /\+ create|новый запуск|create\s+run/i).click()
  cy.get('#run_name').type(runName)
  cy.contains('button', /создать запуск|create/i).click()
  cy.contains(runName).should('be.visible')
})

Cypress.Commands.add('logout', () => {
  cy.contains('button', /logout|выход|выйти/i).click()
  cy.url().should('include', '/login')
})
