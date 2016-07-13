angular.module('event', ['ui.ace']).controller('evController', ['$scope', '$http', function ($scope, $http) {
    $scope.applications = [];
    $scope.currentApp = null;
    $scope.showCreation = true;
    $scope.showAppDetails = false;
    $scope.showJsonEditor = false;
    $scope.showJSEditor = false;

resources = [
{id:0, name:'Deployment Plan'},
{id:1, name:'Static Resources'},
{id:2, name:'Handlers'},
];
    $scope.resources = resources;

function resetCreateApp() {
    $scope.currentApp = null;
    $scope.newApplication = { id : '',
        name : '',
deploy : false,
    };
}

function createApplication(application) {
    if (application.name.length > 0) {
        application.id = $scope.applications.length;
        application.deploy = false;
        application.expand = false;
        application.depcfg = '{"_comment": "Enter deployment configuration"}';
        application.handlers = "/* Enter handlers code here */";
        /*for (i=0; i < resources.length; i++) {
          application.resources.push(resources[i]);
          }*/
        $scope.applications.push(application);
    }
    resetCreateApp()
}

$scope.createApplication = createApplication;

function disableShowOptons() {
    $scope.showCreation = false;
    $scope.showAppDetails = false;
    $scope.showJSEditor = false;
    $scope.showJsonEditor = false;
}

function setCreation() {
    disableShowOptons();
    $scope.showCreation = true;
    $scope.currentApp = null;
}

$scope.setCreation = setCreation;

function setCurrentApp(application) {
    disableShowOptons();
    application.expand = !application.expand;
    $scope.currentApp = application;
    $scope.showAppDetails = true;
}
$scope.setCurrentApp = setCurrentApp;

function deployApplication() {
    $scope.currentApp.deploy = true;
    var res = $http.post('http://localhost:6061/set_application/', $scope.currentApp);
    res.success(function(data, status, headers, config) {
        $scope.set_application = data;
    });
    res.error(function(data, status, headers, config) {
        alert( "failure message: " + JSON.stringify({data: data}));
    });
}
$scope.deployApplication = deployApplication;

function undeployApplication() {
    $scope.currentApp.deploy = false;
}
$scope.undeployApplication = undeployApplication;

function isCurrentApp(application) {
    var flag = $scope.currentApp !== null && application.name === $scope.currentApp.name;
    if (!flag) application.expand = false;
    return flag;
}
$scope.isCurrentApp = isCurrentApp;

function openEditor(resource) {
    disableShowOptons();
    /* Do not edit static resources now */
    switch (resource.id) {
        case 0:
            $scope.showJsonEditor = true;
            break;
        case 1:
            disableShowOptons();
            break;
        case 2:
            $scope.showJSEditor = true;
            break;
    }
}
$scope.openEditor = openEditor;

}]);

