/**
 * Copyright (C) 2013-2014 OpenMediaVault Plugin Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// require("js/omv/WorkspaceManager.js")
// require("js/omv/workspace/form/Panel.js")
// require("js/omv/form/plugin/LinkedFields.js")
// require("js/omv/Rpc.js")
// require("js/omv/data/Store.js")
// require("js/omv/data/Model.js")
// require("js/omv/data/proxy/Rpc.js")

Ext.define("OMV.module.admin.service.deluge.Settings", {
    extend : "OMV.workspace.form.Panel",
    uses : [
        "OMV.data.Model",
        "OMV.data.Store"
    ],

    rpcService   : "Deluge",
    rpcGetMethod : "getSettings",
    rpcSetMethod : "setSettings",

    plugins      : [{
        ptype        : "linkedfields",
        correlations : [{
            name       : [
                "main",
                "webui",
            ],
            conditions : [
                { name  : "done", value : false }
            ],
            properties : "!show"
        },{
            name       : [
                "install",
            ],
            conditions : [
                { name  : "done", value : true }
            ],
            properties : "!show"
        },{
            name       : [
                "port",
            ],
            properties : "!show"
        },{
            name       : [
                "done",
            ],
            properties : "!show"
        }]
    }],

    initComponent : function () {
        var me = this;

        me.on("load", function () {
            var checked = me.findField("enable").checked;
            var showtab = me.findField("showtab").checked;
            var parent = me.up("tabpanel");

            if (!parent)
                return;

            var managementPanel = parent.down("panel[title=" + _("Web Interface") + "]");

            if (managementPanel) {
                checked ? managementPanel.enable() : managementPanel.disable();
                showtab ? managementPanel.tab.show() : managementPanel.tab.hide();
            }
        });

        me.callParent(arguments);
    },

    getButtonItems: function() {
        var items = this.callParent(arguments);

        items.push({
            id: this.getId() + "-show",
            xtype: "button",
            text: _("Open Web Client"),
            icon: "images/deluge.png",
            iconCls: Ext.baseCSSPrefix + "btn-icon-16x16",
            scope: this,
            handler: function() {
                var port = this.getForm().findField("port").getValue();
                var link = "http://" + location.hostname + ":" + port + "/";
                window.open(link, "_blank");
            }
        }, {
            id: this.getId() + '-backup',
            xtype: 'button',
            text: _('Backup'),
            icon: 'images/wrench.png',
            iconCls: Ext.baseCSSPrefix + 'btn-icon-16x16',
            scope: this,
            handler: Ext.Function.bind(this.onBackupButton, this)
        }, {
            id: this.getId() + '-restore',
            xtype: 'button',
            text: _('Restore'),
            icon: 'images/wrench.png',
            iconCls: Ext.baseCSSPrefix + 'btn-icon-16x16',
            scope: this,
            handler: Ext.Function.bind(this.onRestoreButton, this)
        }, {
            id: this.getId() + '-resetd',
            xtype: 'button',
            text: _('Reset_Install'),
            icon: 'images/wrench.png',
            iconCls: Ext.baseCSSPrefix + 'btn-icon-16x16',
            scope: this,
            handler: Ext.Function.bind(this.onResetdButton, this)
        });

        return items;
    },

    getFormItems : function() {
        var me = this;

        return [{
            xtype    : "fieldset",
            title    : "General settings",
            name     : "main",
            defaults : {
                labelSeparator : ""
            },
            items : [{
                xtype      : "checkbox",
                name       : "enable",
                fieldLabel : _("Enable"),
                checked    : false
            },{
                xtype      : "checkbox",
                name       : "showtab",
                fieldLabel : _("Show Tab"),
                boxLabel   : _("Show tab containing Deluge web interface frame."),
                checked    : false
            },{
                xtype      : "checkbox",
                name       : "done",
                fieldLabel : _("Show Tab"),
                boxLabel   : _("Show tab containing Deluge web interface frame."),
                checked    : false
            },{
                xtype: "numberfield",
                name: "port",
                fieldLabel: _("Port"),
                vtype: "port",
                minValue: 1,
                maxValue: 65535,
                allowDecimals: false,
                allowBlank: false,
                value: 8112
            }]
            },{
                xtype         : "fieldset",
                title         : _("Deluge Installer"),
                name          : "install",
                fieldDefaults : {
                    labelSeparator: ""
                },
                items: [{
                    xtype   : "button",
                    text    : _("Install Deluge"),
                    scope   : this,
                    handler : Ext.Function.bind(me.onInstallButton, me, [ me ]),
                    margin : "5 5 5 5"
            },{
                    xtype   : "button",
                    text    : _("Install Deluge 1.3.5"),
                    scope   : this,
                    handler : Ext.Function.bind(me.onOldButton, me, [ me ]),
                    margin : "5 5 5 5"
            },{
                border : false,
                html   : "<ul><li>" + ("Main setting will apear after you have installed Deluge.") + "</li></ul>" +
                         "<ul><li>" + ("Remember the default webui password is deluge") + "</li></ul>"
            }]
        }];
    },

    onBackupButton: function() {
        OMV.Download.request('Deluge', 'downloadBackup');
    },

    doonResetd: function() {
        var me = this;
        var wnd = Ext.create("OMV.window.Execute", {
            title           : _("Cleaning Apt Files and Lists..."),
            rpcService      : "Deluge",
            rpcMethod       : "doReset",
            rpcIgnoreErrors : true,
            hideStartButton : true,
            hideStopButton  : true,
            listeners       : {
                scope     : me,
                finish    : function(wnd, response) {
                    wnd.appendValue(_("Done..."));
                    wnd.setButtonDisabled("close", false);
                },
                exception : function(wnd, error) {
                    OMV.MessageBox.error(null, error);
                    wnd.setButtonDisabled("close", false);
                },
                close     : function() {
                    me.doReload();
                }
            }
        }).show();
        wnd.setButtonDisabled("close", true);
        wnd.show();
        wnd.start();
    },

    onResetdButton: function() {
        var me = this;
        var wnd = "test";
        me.doSubmit();
        Ext.create("OMV.window.Execute", {
            title      : _("Deluge Reseter"),
            rpcService : "Deluge",
            rpcMethod  : "doReset",
            listeners  : {
                scope     : me,
                exception : function(wnd, error) {
                    OMV.MessageBox.error(null, error);
                },
                close : function() {
                    me.doReload();
                },
                finish : function() {
                                    me.doReload();
                                }
            }
        }).show();
    },

    onRestoreButton: function() {
        Ext.create('OMV.window.Upload', {
            title: _('Upload backup'),
            service: 'Deluge',
            method: 'uploadBackup',
            listeners: {
                scope: this,
                success: function(wnd, response) {
                    OMV.MessageBox.info(_('Restored backup'), _('Backup was successfully restored.'));
                }
            }
        }).show();
    },

    onInstallButton: function() {
        var me = this;
        me.doSubmit();
        Ext.create("OMV.window.Execute", {
            title      : _("Deluge Installer"),
            rpcService : "Deluge",
            rpcMethod  : "doInstall",
            listeners  : {
                scope     : me,
                exception : function(wnd, error) {
                    OMV.MessageBox.error(null, error);
                },
                finish : function() {
                                    me.doReload();
                                }
            }
        }).show();
    },

    onOldButton: function() {
        var me = this;
        me.doSubmit();
        Ext.create("OMV.window.Execute", {
            title      : _("Deluge Installer"),
            rpcService : "Deluge",
            rpcMethod  : "doOld",
            listeners  : {
                scope     : me,
                exception : function(wnd, error) {
                    OMV.MessageBox.error(null, error);
                },
                finish : function() {
                                    me.doReload();
                                }
            }
        }).show();
    }
});

OMV.WorkspaceManager.registerPanel({
    id        : "settings",
    path      : "/service/deluge",
    text      : _("Settings"),
    position  : 10,
    className : "OMV.module.admin.service.deluge.Settings"
});
