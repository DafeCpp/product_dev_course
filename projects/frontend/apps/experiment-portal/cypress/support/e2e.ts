/// <reference types="cypress" />

Cypress.Commands.add('loginAsAdmin', () => {
  cy.visit('/login')
  cy.get('input[type="text"]').type('admin')
  cy.get('input[type="password"]').type('Admin123')
  cy.contains('button', /вход|войти/i).click()
  cy.url().should('match', /\/(projects|experiments)/)
})

Cypress.Commands.add('createProject', (projectName: string) => {
  cy.contains('button', /\+ create|новый проект/i).click()
  cy.get('input[name="project_name"]').type(projectName)
  cy.contains('button', /save|создать|сохранить/i).click()
  cy.contains(projectName).should('be.visible')
})

Cypress.Commands.add('openProject', (projectName: string) => {
  cy.contains('a, button, div', projectName)
    .closest('[role="button"], button, a')
    .click()
  cy.url().should('include', '/experiments')
})

Cypress.Commands.add('createExperiment', (experimentName: string) => {
  cy.contains('button', /\+ create|новый эксперимент/i).click()
  cy.get('input[name="name"]').type(experimentName)
  cy.contains('button', /save|создать|сохранить/i).click()
  cy.contains(experimentName).should('be.visible')
})

Cypress.Commands.add('openExperiment', (experimentName: string) => {
  cy.contains('a, button, div', experimentName)
    .closest('[role="button"], button, a')
    .click()
  cy.url().should('include', '/runs')
})

Cypress.Commands.add('createRun', () => {
  cy.contains('button', /\+ create|новый запуск|start/i).click()
  cy.get('button').contains(/save|create|start|запустить/i, { timeout: 5000 }).click()
  cy.contains(/run|запуск/i).should('be.visible')
})

Cypress.Commands.add('logout', () => {
  cy.contains('button', /logout|выход|выйти/i).click()
  cy.url().should('include', '/login')
})
