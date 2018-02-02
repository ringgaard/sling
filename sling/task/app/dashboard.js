var app = angular.module('DashboardApp', ['ngRoute', 'ngMaterial']);

app.config(function ($locationProvider) {
  $locationProvider.html5Mode(true);
})

app.controller('DashboardCtrl', function($scope, $location, $rootScope) {
  $scope.host = function() {
    return $location.host() + ":" + $location.port();
  };

  $rootScope.refresh = function() {
    $rootScope.$emit('refresh');
  };
})

app.controller('StatusCtrl', function($scope, $http, $rootScope) {
  $scope.status = null;
  $scope.jobs = [];
  $scope.selected = 0;

  $rootScope.$on('refresh', function(event, args) {
    console.log("refresh");
    $scope.refresh();
  });

  $scope.update = function() {
    console.log("update");
    var status = $scope.status

    // Update job list.
    var jobs = [];
    for (var i = 0; i < status.jobs.length; ++i) {
      job = status.jobs[i];
      job.time = status.time;
      item = {};
      item.id = i;
      item.name = job.name;
      item.job = job;

      // Compute elapsed time for job.
      var ended = job.ended ? job.ended : job.time;
      var elapsed = ended - job.started;
      var hours = Math.floor(elapsed / 3600);
      var mins = Math.floor((elapsed % 3600) / 60);
      var secs = Math.floor(elapsed % 60);
      item.time = hours + "h  " + mins + "m " + secs + "s";

      // Compute task progress for job.
      var progress = "";
      if (job.stages) {
        for (var j = 0; j < job.stages.length; ++j) {
          var stage = job.stages[j];
          if (j > 0) progress += "│ ";
          progress += "█ ".repeat(stage.done);
          progress += "░ ".repeat(stage.tasks - stage.done);
        }
      } else {
        progress = "✔";
      }
      item.status = progress;

      jobs.push(item);
    }
    $scope.jobs = jobs;

    // Update job status.
    $rootScope.$emit('job', $scope.status.jobs[$scope.selected]);
  }

  $scope.refresh = function() {
    console.log('Fetch status');
    $http.get('/status').then(function(results) {
      $scope.status = results.data;
      console.log('status', $scope.status);
      $scope.update($scope.status);
    });
  }

  $scope.select = function(id) {
    console.log("select", id);
    $scope.selected = id;
    $rootScope.$emit('job', $scope.status.jobs[id]);
  }

  $scope.refresh();
})

app.controller('JobCtrl', function($scope, $rootScope) {
  $scope.job = null;
  $scope.counters = null;
  $scope.prev_counters = null;
  $scope.prev_time = null;

  $scope.refresh = function() {
    var job = $scope.job;
    console.log("refresh", job);
    var ctrs = [];
    var prev = $scope.prev_counters;
    var elapsed = job.time - $scope.prev_time;
    for (name in job.counters) {
      item = {};
      item.name = name;
      item.value = job.counters[name];
      if (prev && elapsed > 0) {
        var prev_value = prev[name];
        var delta = item.value - prev_value;
        item.rate = delta / elapsed;
      }
      ctrs.push(item);
    }
    $scope.counters = ctrs;
    $scope.prev_counters = job.counters;
    $scope.prev_time = job.time;
  }

  $scope.hasJob = function() {
    return $scope.job != null;
  }

  $rootScope.$on('job', function(event, args) {
    $scope.job = args;
    $scope.refresh();
  });
})

