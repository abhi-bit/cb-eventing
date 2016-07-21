var ev = angular.module('event', ['ui.ace', 'createApp']);

ev.run(['$rootScope', '$http', function($rootScope, $http) {
    $rootScope.applications = [];
    $http.get('http://localhost:6061/get_application')
    .then(function(response) {
        for(var i = 0; i < response.data.length; i++) {
            $rootScope.applications.push(response.data[i]);
        }
    });
}]);

ev.value('resources', resources = [
    {id:0, name:'Deployment Plan'},
    {id:1, name:'Static Resources'},
    {id:2, name:'Handlers'},
]);

ev.controller('evController', ['$scope', '$http', 'resources',
    function ($scope, $http, resources) {
        $scope.currentApp = null;
        $scope.showCreation = true;
        $scope.showAppDetails = false;
        $scope.showJsonEditor = false;
        $scope.showJSEditor = false;

        $scope.resources = resources;

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
    var uri = 'http://localhost:6061/set_application/?name=' + $scope.currentApp.name;
    console.log('Setting URI: ', uri);
    var res = $http.post(uri, $scope.currentApp);
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

