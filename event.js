angular.module('event', []).controller('evController', function ($scope) {
    $scope.applications = [];
    $scope.currentApp = null;

    resources = [
{id:0, name:'Deployment Plan'},
{id:1, name:'Static Resources'},
{id:2, name:'Handlers'},
];

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
        application.resources=resources;
        /*for (i=0; i < resources.length; i++) {
          application.resources.push(resources[i]);
          }*/
        $scope.applications.push(application);
    }
    resetCreateApp()
}

$scope.createApplication = createApplication;

$scope.showCreation = true;

function setCreation() {
    $scope.showCreation = true;
    $scope.currentApp = null;
}

$scope.setCreation = setCreation;

function disableCreation() {
    $scope.showCreation = false;
}


function setCurrentApp(application) {
    disableCreation();
    application.expand = !application.expand;
    $scope.currentApp = application;
}
$scope.setCurrentApp = setCurrentApp;

function deployApplication() {
    $scope.currentApp.deploy = true;
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
});

