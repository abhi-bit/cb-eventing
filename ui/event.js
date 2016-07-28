(function() {
    var ev = angular.module('event', ['ui.ace', 'ui.router']);
    var applications = [];
    var resources = [
        {id:0, name:'Deployment Plan'},
        {id:1, name:'Static Resources'},
        {id:2, name:'Handlers'},
    ];

    ev.config(['$stateProvider', '$urlRouterProvider', function($stateProvider, $urlRouterProvider) {
        $urlRouterProvider.otherwise('/');
        $stateProvider
        .state('applications', {
            url: '/',
            templateUrl: 'templates/createApp-frag.html',
            controller: 'CreateController',
            controllerAs: 'createCtrl'
        })
        .state('appName', {
            url: '/:appName',
            templateUrl: 'templates/applications-frag.html',
            controller: 'PerAppController',
            controllerAs: 'perAppCtrl',
        })
        .state('resName', {
            url: '/:appName/:resName',
            templateUrl: 'templates/editor-frag.html',
            controller: 'ResEditorController',
            controllerAs: 'resEditCtrl',
        });

    }]);

    ev.run(['$http', function($http) {
        $http.get('http://localhost:6061/get_application')
        .then(function(response) {
            for(var i = 0; i < response.data.length; i++) {
                applications.push(response.data[i]);
            }
        });
    }]);

    ev.directive('appHeader', function(){
        return {
            restrict: 'E',
            templateUrl: 'templates/header-frag.html',
        };
    });

    ev.directive('appListsLeftPanel', function(){
        return {
            restrict: 'E',
            templateUrl: 'templates/applist-frag.html',
            controller: 'AppListController',
            controllerAs: 'appListCtrl',
        };
    });


    ev.controller('CreateController',[function() {
        this.showCreation = true;
        this.applications = applications;
        this.createApplication = function(application) {
            if (application.name.length > 0) {
                application.id = this.applications.length;
                application.deploy = false;
                application.expand = false;
                application.depcfg = '{"_comment": "Enter deployment configuration"}';
                application.handlers = "/* Enter handlers code here */";
                this.applications.push(application);
            }
            this.newApplication={};
        }
    }]);

    ev.controller('AppListController', [function() {
        this.resources = resources;
        this.applications = applications;
        this.currentApp = null;
        this.setCurrentApp = function (application) {
            application.expand = !application.expand;
            this.currentApp = application;
        }
        this.isCurrentApp = function(application) {
            var flag = this.currentApp !== null && application.name === this.currentApp.name;
            if (!flag) application.expand = false;
            return flag;
        }
    }]);

    ev.controller('PerAppController', ['$location', '$http', function($location, $http) {
        this.currentApp = null;
        var appName = $location.path().slice(1);
        for(var i = 0; i < applications.length; i++) {
            if(applications[i].name === appName) {
                this.currentApp = applications[i];
                break;
            }
        }

        this.deployApplication = function() {
            this.currentApp.deploy = true;
            this.currentApp.depcfg = JSON.parse(this.currentApp.depcfg);
            var uri = 'http://localhost:6061/set_application/?name=' + this.currentApp.name;
            var res = $http.post(uri, this.currentApp);
            res.success(function(data, status, headers, config) {
                this.set_application = data;
            });
            res.error(function(data, status, headers, config) {
                alert( "failure message: " + JSON.stringify({data: data}));
            });
        }

        this.undeployApplication = function() {
            this.currentApp.deploy = false;
        }

    }]);

    ev.controller('ResEditorController', ['$location', function($location){
        this.currentApp = null;
        var values = $location.path().split('/');
        appName = values[1];
        for(var i = 0; i < applications.length; i++) {
            if(applications[i].name === appName) {
                this.currentApp = applications[i];
                break;
            }
        }
        if(values[2] == 'Deployment Plan') {
            this.showJsonEditor = true;
            this.showJSEditor = false;
        }
        else if(values[2] == 'Handlers') {
            this.showJsonEditor = false;
            this.showJSEditor = true;
        }
        else {
            this.showJSEditor = false;
            this.showJsonEditor = false;
        }
    }]);

})();
