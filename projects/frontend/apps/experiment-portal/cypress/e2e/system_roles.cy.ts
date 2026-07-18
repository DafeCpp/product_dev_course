/// <reference types="cypress" />

describe('System roles', () => {
  let createdRoleId: string | undefined

  afterEach(() => {
    if (createdRoleId) {
      cy.request({
        method: 'DELETE',
        url: `/api/v1/system-roles/${createdRoleId}`,
        failOnStatusCode: false,
      })
    }
  })

  it('displays API permissions and preserves them when editing a custom role', () => {
    const roleName = `E2E System Role ${Date.now()}`

    cy.loginAsAdmin()
    cy.intercept('GET', '/api/v1/system-roles').as('getSystemRoles')
    cy.intercept('POST', '/api/v1/system-roles').as('createSystemRole')

    cy.visit('/admin/system-roles', {
      onBeforeLoad(win) {
        cy.stub(win.console, 'error').as('consoleError')
      },
    })
    cy.wait('@getSystemRoles')

    cy.contains('tr', 'admin').within(() => {
      cy.contains('roles.manage').should('be.visible')
      cy.contains('audit.read').should('be.visible')
    })
    cy.contains('tr', 'superadmin').within(() => {
      cy.contains('—').should('be.visible')
    })

    cy.contains('button', 'Создать роль').click()
    cy.get('#role-name').type(roleName)
    cy.contains('.permission-picker__item-name', 'roles.manage')
      .closest('label')
      .find('input[type="checkbox"]')
      .check()
    cy.contains('.permission-picker__item-name', 'audit.read')
      .closest('label')
      .find('input[type="checkbox"]')
      .check()
    cy.contains('button', 'Сохранить').click()

    cy.wait('@createSystemRole').then((interception) => {
      expect(interception.response?.statusCode).to.equal(201)
      createdRoleId = interception.response?.body.id as string
    })
    cy.contains('tr', roleName).within(() => {
      cy.contains('roles.manage').should('be.visible')
      cy.contains('audit.read').should('be.visible')
      cy.contains('button', 'Редактировать').click()
    })

    cy.contains('.permission-picker__item-name', 'roles.manage')
      .closest('label')
      .find('input[type="checkbox"]')
      .should('be.checked')
    cy.contains('.permission-picker__item-name', 'audit.read')
      .closest('label')
      .find('input[type="checkbox"]')
      .should('be.checked')

    cy.get('@consoleError').should((consoleError) => {
      expect(consoleError).not.to.have.been.calledWithMatch(
        'Each child in a list should have a unique "key" prop'
      )
    })
  })
})
