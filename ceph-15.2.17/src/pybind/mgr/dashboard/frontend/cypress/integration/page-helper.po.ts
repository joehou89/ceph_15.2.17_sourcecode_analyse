interface Page {
  url: string;
  id: string;
}

export abstract class PageHelper {
  pages: Record<string, Page>;

  /**
   * Decorator to be used on Helper methods to restrict access to one particular URL.  This shall
   * help developers to prevent and highlight mistakes.  It also reduces boilerplate code and by
   * thus, increases readability.
   */
  static restrictTo(page: string): Function {
    return (target: any, propertyKey: string, descriptor: PropertyDescriptor) => {
      const fn: Function = descriptor.value;
      descriptor.value = function (...args: any) {
        cy.location('hash').should((url) => {
          expect(url).to.eq(
            page,
            `Method ${target.constructor.name}::${propertyKey} is supposed to be ` +
              `run on path "${page}", but was run on URL "${url}"`
          );
        });
        fn.apply(this, args);
      };
    };
  }

  /**
   * Navigates to the given page or to index.
   * Waits until the page component is loaded
   */
  navigateTo(name: string = null) {
    name = name || 'index';
    const page = this.pages[name];

    cy.visit(page.url);
    cy.get(page.id);
  }

  /**
   * Navigates back and waits for the hash to change
   */
  navigateBack() {
    cy.location('hash').then((hash) => {
      cy.go('back');
      cy.location('hash').should('not.be', hash);
    });
  }

  /**
   * Navigates to the edit page
   */
  navigateEdit(name: string, select = true) {
    if (select) {
      this.navigateTo();
      this.getFirstTableCell(name).click();
    }
    cy.contains('Creating...').should('not.exist');
    cy.contains('button', 'Edit').click();
    this.expectBreadcrumbText('Edit');
  }

  /**
   * Checks the active breadcrumb value.
   */
  expectBreadcrumbText(text: string) {
    cy.get('.breadcrumb-item.active').should('have.text', text);
  }

  getTabs() {
    return cy.get('.nav.nav-tabs li');
  }

  getTab(tabName: string) {
    return cy.contains('.nav.nav-tabs li', new RegExp(`^${tabName}$`));
  }

  getTabText(index: number) {
    return this.getTabs().its(index).text();
  }

  getTabsCount(): any {
    return this.getTabs().its('length');
  }

  /**
   * Helper method to select an option inside a select element.
   * This method will also expect that the option was set.
   * @param option The option text (not value) to be selected.
   */
  selectOption(selectionName: string, option: string) {
    cy.get(`select[name=${selectionName}]`).select(option);
    return this.expectSelectOption(selectionName, option);
  }

  /**
   * Helper method to expect a set option inside a select element.
   * @param option The selected option text (not value) that is to
   *   be expected.
   */
  expectSelectOption(selectionName: string, option: string) {
    return cy.get(`select[name=${selectionName}] option:checked`).contains(option);
  }

  getLegends() {
    return cy.get('legend');
  }

  getToast() {
    return cy.get('.ngx-toastr');
  }

  /**
   * Waits for the table to load its data
   * Should be used in all methods that access the datatable
   */
  private waitDataTableToLoad() {
    cy.get('cd-table').should('exist');
    cy.get('datatable-scroller, .empty-row');
  }

  getDataTables() {
    this.waitDataTableToLoad();

    return cy.get('cd-table .dataTables_wrapper');
  }

  private getTableCountSpan(spanType: 'selected' | 'found' | 'total') {
    return cy.contains('.datatable-footer-inner .page-count span', spanType);
  }

  // Get 'selected', 'found', or 'total' row count of a table.
  getTableCount(spanType: 'selected' | 'found' | 'total') {
    this.waitDataTableToLoad();
    return this.getTableCountSpan(spanType).then(($elem) => {
      const text = $elem
        .filter((_i, e) => e.innerText.includes(spanType))
        .first()
        .text();

      return Number(text.match(/(\d+)\s+\w*/)[1]);
    });
  }

  // Wait until selected', 'found', or 'total' row count of a table equal to a number.
  expectTableCount(spanType: 'selected' | 'found' | 'total', count: number) {
    this.waitDataTableToLoad();
    this.getTableCountSpan(spanType).should(($elem) => {
      const text = $elem.first().text();
      expect(Number(text.match(/(\d+)\s+\w*/)[1])).to.equal(count);
    });
  }

  getTableRow(content: string) {
    this.waitDataTableToLoad();

    this.seachTable(content);
    return cy.contains('.datatable-body-row', content);
  }

  getTableRows() {
    this.waitDataTableToLoad();

    return cy.get('datatable-row-wrapper');
  }

  /**
   * Returns the first table cell.
   * Optionally, you can specify the content of the cell.
   */
  getFirstTableCell(content?: string) {
    this.waitDataTableToLoad();

    if (content) {
      this.seachTable(content);
      return cy.contains('.datatable-body-cell-label', content);
    } else {
      return cy.get('.datatable-body-cell-label').first();
    }
  }

  getTableCell(columnIndex: number, exactContent: string) {
    this.waitDataTableToLoad();
    this.seachTable(exactContent);
    return cy.contains(
      `datatable-body-row datatable-body-cell:nth-child(${columnIndex})`,
      new RegExp(`^${exactContent}$`)
    );
  }

  getExpandCollapseElement(content?: string) {
    this.waitDataTableToLoad();

    if (content) {
      return cy.contains('.datatable-body-row', content).find('.tc_expand-collapse');
    } else {
      return cy.get('.tc_expand-collapse').first();
    }
  }

  /**
   * Gets column headers of table
   */
  getDataTableHeaders(index = 0) {
    this.waitDataTableToLoad();

    return cy.get('.datatable-header').its(index).find('.datatable-header-cell-label');
  }

  /**
   * Grabs striped tables
   */
  getStatusTables() {
    return cy.get('.table.table-striped');
  }

  filterTable(name: string, option: string) {
    this.waitDataTableToLoad();

    cy.get('.tc_filter_name > a').click();
    cy.contains(`.tc_filter_name .dropdown-item`, name).click();

    cy.get('.tc_filter_option > a').click();
    cy.contains(`.tc_filter_option .dropdown-item`, option).click();
  }

  setPageSize(size: string) {
    cy.get('cd-table .dataTables_paginate input').first().clear().type(size);
  }

  seachTable(text: string) {
    this.waitDataTableToLoad();

    this.setPageSize('10');
    cy.get('cd-table .search input').first().clear().type(text);
  }

  clearTableSearchInput() {
    this.waitDataTableToLoad();

    return cy.get('cd-table .search button').click();
  }

  // Click the action button
  clickActionButton(action: string) {
    cy.get('.table-actions button.dropdown-toggle').first().click(); // open submenu
    cy.get(`.table-actions li.${action}`).click(); // click on "action" menu item
  }

  /**
   * This is a generic method to delete table rows.
   * It will select the first row that contains the provided name and delete it.
   * After that it will wait until the row is no longer displayed.
   * @param name The string to search in table cells.
   * @param columnIndex If provided, search string in columnIndex column.
   */
  delete(name: string, columnIndex?: number) {
    // Selects row
    const getRow = columnIndex
      ? this.getTableCell.bind(this, columnIndex)
      : this.getFirstTableCell.bind(this);
    getRow(name).click();

    // Clicks on table Delete button
    this.clickActionButton('delete');

    // Confirms deletion
    cy.get('cd-modal .custom-control-label').click();
    cy.contains('cd-modal button', 'Delete').click();

    // Wait for modal to close
    cy.get('cd-modal').should('not.exist');

    // Waits for item to be removed from table
    getRow(name).should('not.exist');
  }
}
