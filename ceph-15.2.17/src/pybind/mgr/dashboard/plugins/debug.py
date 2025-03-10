# -*- coding: utf-8 -*-
from __future__ import absolute_import

from enum import Enum
import json

from . import PLUGIN_MANAGER as PM
from . import interfaces as I  # noqa: E741,N812
from .plugin import SimplePlugin as SP

try:
    from typing import no_type_check
except ImportError:
    no_type_check = object()  # Just for type checking


class Actions(Enum):
    ENABLE = 'enable'
    DISABLE = 'disable'
    STATUS = 'status'


@PM.add_plugin  # pylint: disable=too-many-ancestors
class Debug(SP, I.CanCherrypy, I.ConfiguresCherryPy,  # pylint: disable=too-many-ancestors
            I.Setupable, I.ConfigNotify):
    NAME = 'debug'

    OPTIONS = [
        SP.Option(
            name=NAME,
            default=False,
            type='bool',
            desc="Enable/disable debug options"
        )
    ]

    @no_type_check  # https://github.com/python/mypy/issues/7806
    def _refresh_health_checks(self):
        debug = self.get_option(self.NAME)
        if debug:
            self.mgr.health_checks.update({'DASHBOARD_DEBUG': {
                'severity': 'warning',
                'summary': 'Dashboard debug mode is enabled',
                'detail': [
                    'Please disable debug mode in production environments using '
                    '"ceph dashboard {} {}"'.format(self.NAME, Actions.DISABLE.value)
                ]
            }})
        else:
            self.mgr.health_checks.pop('DASHBOARD_DEBUG', None)
        self.mgr.refresh_health_checks()

    @PM.add_hook
    def setup(self):
        self._refresh_health_checks()

    @no_type_check
    def handler(self, action):
        ret = 0
        msg = ''
        if action in [Actions.ENABLE.value, Actions.DISABLE.value]:
            self.set_option(self.NAME, action == Actions.ENABLE.value)
            self.mgr.update_cherrypy_config({})
            self._refresh_health_checks()
        else:
            debug = self.get_option(self.NAME)
            msg = "Debug: '{}'".format('enabled' if debug else 'disabled')
        return ret, msg, None

    COMMANDS = [
        SP.Command(
            prefix="dashboard {name}".format(name=NAME),
            args="name=action,type=CephChoices,strings={states}".format(
                states="|".join(a.value for a in Actions)),
            desc="Control and report debug status in Ceph-Dashboard",
            handler=handler
        )
    ]

    def custom_error_response(self, status, message, traceback, version):
        self.response.headers['Content-Type'] = 'application/json'
        error_response = dict(status=status, detail=message, request_id=str(self.request.unique_id))

        if self.get_option(self.NAME):
            error_response.update(dict(traceback=traceback, version=version))

        return json.dumps(error_response)

    @PM.add_hook
    def configure_cherrypy(self, config):
        config.update({
            'environment': 'test_suite' if self.get_option(self.NAME) else 'production',
            'error_page.default': self.custom_error_response,
        })

    @PM.add_hook
    def config_notify(self):
        self._refresh_health_checks()
