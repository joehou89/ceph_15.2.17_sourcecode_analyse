import { Input, ManagerModulesPageHelper } from './mgr-modules.po';

describe('Manager modules page', () => {
  const mgrmodules = new ManagerModulesPageHelper();

  beforeEach(() => {
    cy.login();
    Cypress.Cookies.preserveOnce('token');
    mgrmodules.navigateTo();
  });

  describe('breadcrumb test', () => {
    it('should open and show breadcrumb', () => {
      mgrmodules.expectBreadcrumbText('Manager Modules');
    });
  });

  describe('verifies editing functionality for manager modules', () => {
    it('should test editing on diskprediction_cloud module', () => {
      const diskpredCloudArr: Input[] = [
        {
          id: 'diskprediction_cert_context',
          newValue: 'Foo',
          oldValue: ''
        },
        {
          id: 'sleep_interval',
          newValue: '456',
          oldValue: '60'
        }
      ];
      mgrmodules.editMgrModule('diskprediction_cloud', diskpredCloudArr);
    });

    it('should test editing on balancer module', () => {
      const balancerArr: Input[] = [
        {
          id: 'crush_compat_max_iterations',
          newValue: '123',
          oldValue: '25'
        }
      ];
      mgrmodules.editMgrModule('balancer', balancerArr);
    });

    it('should test editing on dashboard module', () => {
      const dashboardArr: Input[] = [
        {
          id: 'RGW_API_USER_ID',
          newValue: 'rq',
          oldValue: ''
        },
        {
          id: 'GRAFANA_API_PASSWORD',
          newValue: 'rafa',
          oldValue: ''
        }
      ];
      mgrmodules.editMgrModule('dashboard', dashboardArr);
    });

    it('should test editing on devicehealth module', () => {
      const devHealthArray: Input[] = [
        {
          id: 'mark_out_threshold',
          newValue: '1987',
          oldValue: '2419200'
        },
        {
          id: 'pool_name',
          newValue: 'sox',
          oldValue: 'device_health_metrics'
        },
        {
          id: 'retention_period',
          newValue: '1999',
          oldValue: '15552000'
        },
        {
          id: 'scrape_frequency',
          newValue: '2020',
          oldValue: '86400'
        },
        {
          id: 'sleep_interval',
          newValue: '456',
          oldValue: '600'
        },
        {
          id: 'warn_threshold',
          newValue: '567',
          oldValue: '7257600'
        }
      ];

      mgrmodules.editMgrModule('devicehealth', devHealthArray);
    });
  });
});
