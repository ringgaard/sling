var app = angular.module('DashboardApp', ['ngRoute', 'ngMaterial']);

app.config(function ($locationProvider) {
  $locationProvider.html5Mode(true);
})

app.controller('DashboardCtrl', function($scope) {
})

app.controller('JobCtrl', function($scope) {
})

