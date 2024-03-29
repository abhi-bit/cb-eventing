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

    ev.run(['$http', '$window', function($http, $window) {
        $http.get('http://localhost:6061/get_application')
        .then(function(response) {
            for(var i = 0; i < response.data.length; i++) {
                response.data[i].depcfg = JSON.stringify(response.data[i].depcfg, null, ' ');
                applications.push(response.data[i]);
            }
        });
        $window.onbeforeunload = function(e) {
            e.preventDefault();
            $window.setTimeout(function () { $window.location = e.srcElement.origin + '/event.html/'; }, 0);
            $window.onbeforeunload = null;
        };
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
                application.assets = [];
                application.debug = false;
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
            var x = angular.copy(this.currentApp);
            x.depcfg = JSON.parse(x.depcfg);
            var uri = 'http://localhost:6061/set_application/?name=' + this.currentApp.name;
            var res = $http.post(uri, x);
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

        this.startDbg = function() {
            this.currentApp.debug = true;
            var uri = 'http://localhost:6061/start_dbg/?name=' + this.currentApp.name;
            var res = $http.post(uri, null);
            res.success(function(data, status, headers, config) {
                this.set_application = data;
            });
            res.error(function(data, status, headers, config) {
                alert( "failure message: " + JSON.stringify({data: data}));
            });
        }
        this.stopDbg = function() {
            this.currentApp.debug = false;
            var uri = 'http://localhost:6061/stop_dbg/?name=' + this.currentApp.name;
            var res = $http.post(uri, null);
            res.success(function(data, status, headers, config) {
                this.set_application = data;
            });
            res.error(function(data, status, headers, config) {
                alert( "failure message: " + JSON.stringify({data: data}));
            });
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
            this.showLoading = false;
        }
        else if(values[2] == 'Handlers') {
            this.showJsonEditor = false;
            this.showJSEditor = true;
            this.showLoading = false;
        }
        else if(values[2] == 'Static Resources') {
            this.showJsonEditor = false;
            this.showJSEditor = false;
            this.showLoading = true;
        }
        else {
            this.showJSEditor = false;
            this.showJsonEditor = false;
            this.showLoading = false;
        }
        this.saveAsset = function(asset, content) {
            this.currentApp.assets.push({name:asset.name, content:content, operation:"add", id:this.currentApp.assets.length});
        };
        this.deleteAsset = function(asset) {
            asset.operation = "delete";
            asset.content = null;
        }
    }]);

    ev.directive('onReadFile', ['$parse', function ($parse) {
        return {
            restrict: 'A',
            scope: false,
            controller: 'ResEditorController',
            controllerAs: 'resEditCtrl',
            link: function(scope, element, attrs) {
                var fn = $parse(attrs.onReadFile);
                element.on('change', function(onChangeEvent) {
                    var reader = new FileReader();
                    reader.onload = function(onLoadEvent) {
                        scope.$apply(function() {
                            fn(scope, {asset : (onChangeEvent.srcElement || onChangeEvent.target).files[0], content : onLoadEvent.target.result});
                        });
                    };
                    reader.readAsDataURL((onChangeEvent.srcElement || onChangeEvent.target).files[0]);
                });
            }
        };
    }]);
})();

