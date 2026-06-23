/// <reference types="cypress" />

Cypress.Commands.add('loginAsAdmin', () => {
  cy.visit('/login')
  cy.get('input[type="text"]').type('admin')
  cy.get('input[type="password"]').type('Admin123')
  cy.contains('button', /вход|войти/i).click()
  cy.url().should('match', /\/(projects|experiments)/)
})

Cypress.Commands.add('createProject', (projectName: string) => {
  cy.contains('button', /\+ create|новый проект|create\s+project/i).click()
  cy.get('#project_modal_name').type(projectName)
  cy.contains('button', /создать проект|save|create/i).click()
  cy.contains(projectName).should('be.visible')
})

Cypress.Commands.add('openProject', (projectName: string) => {
  cy.contains('a, button, div', projectName)
    .closest('[role="button"], button, a')
    .click()
  cy.url().should('include', '/experiments')
})

Cypress.Commands.add('createExperiment', (experimentName: string) => {
  cy.contains('button', /\+ create|новый эксперимент|create\s+experiment/i).click()
  cy.get('#experiment_name').type(experimentName)
  cy.contains('button', /создать эксперимент|create/i).click()
  cy.contains(experimentName).should('be.visible')
})

Cypress.Commands.add('openExperiment', (experimentName: string) => {
  cy.contains('a, button, div', experimentName)
    .closest('[role="button"], button, a')
    .click()
  cy.url().should('include', '/runs')
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
