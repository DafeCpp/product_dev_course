/// <reference types="cypress" />

// Deterministic UI integration tests. Backend lifecycle tests remain separate.
describe('shared React primitives', () => {
  beforeEach(() => {
    cy.intercept('GET', '**/auth/me', { id: 'p10-user', email: 'p10@example.com', is_active: true, is_admin: true, password_change_required: false })
    cy.intercept('GET', '**/effective-permissions*', { is_superadmin: true, system_permissions: [], project_permissions: [] })
  })

  for (const width of [390, 1440]) {
    it(`shows loading, empty state and project form actions at ${width}px`, () => {
      cy.viewport(width, 900)
      cy.intercept('GET', '**/api/v1/projects*', { delay: 500, body: { items: [], total: 0 } }).as('projects')
      cy.visit('/projects')
      cy.get('[role="status"]').should('contain', 'Загрузка проектов')
      cy.wait('@projects')
      cy.get('.lp-empty-state').should('contain', 'У вас пока нет проектов')
      cy.document().then((doc) => expect(doc.documentElement.scrollWidth).to.be.at.most(width))
      cy.screenshot(`shared-projects-${width}`)
      cy.get('.lp-empty-state button').click()
      cy.get('.modal-content').within(() => {
        cy.get('.lp-form-actions').should('be.visible')
        cy.contains('button', 'Создать проект').should('be.disabled')
        cy.get('input').first().type('Общий проект')
        cy.contains('button', 'Создать проект').should('be.enabled')
        cy.contains('button', 'Отмена').click()
      })
      cy.get('.modal-content').should('not.exist')
      cy.get('.lp-empty-state').should('be.visible')
      cy.visit('/sensors/new')
      cy.get('.lp-page-header h2').should('have.text', 'Зарегистрировать датчик')
      cy.document().then((doc) => expect(doc.documentElement.scrollWidth).to.be.at.most(width))
    })
  }

  it('announces a failed projects request', () => {
    cy.intercept('GET', '**/api/v1/projects*', { statusCode: 500, body: { error: 'Test failure' } })
    cy.visit('/projects')
    cy.get('.lp-error-state[role="alert"]').should('be.visible')
    cy.get('.lp-empty-state').should('not.exist')
  })
})
