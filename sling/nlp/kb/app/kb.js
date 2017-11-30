var app = angular.module('KnowledgeBaseApp', ['ngMaterial']);

app.controller('KnowledgeBaseCtrl', function($scope, $mdSidenav, $http, $location) {
  $scope.applications = [
    {"title": "Knowledge base", "url": "", "icon": "search"}
  ];

  $scope.active = {
    "title": "Knowledge base",
    "url": ""
  };

  $scope.openSideNavPanel = function() {
    $mdSidenav('left').open();
  };

  $scope.closeSideNavPanel = function() {
    $mdSidenav('left').close();
  };
  
  $scope.switchTo = function(appl) {
    $mdSidenav('left').close();
    $scope.active = appl;
    $location.path(appl.name);
  }
})

app.controller('SearchCtrl', function($scope, $http, $rootScope) {
  var self = this;
  self.query = "";
  self.selected = null;

  self.querySearch = function(query) {
    if (query == "") return null;
    return $http.get('/kb/query?fmt=cjson&q=' + query)
      .then(function(results) {
        return results.data.matches;
      });
  }

  self.selectedItemChange = function(item) {
    console.log('Item changed to ' + item.ref);
    $rootScope.$broadcast('item', item.ref);
  }
})

app.controller('ItemCtrl', function($scope, $http) {
  var commons_service = "https://commons.wikimedia.org/w/api.php?" +
                         "callback=JSON_CALLBACK&" +
                         "action=query&prop=imageinfo&iiprop=url&redirects&" +
                         "format=json&iiurlwidth=400&titles=File:"
  var commons_url = "https://commons.wikimedia.org/wiki/File:";

  $scope.item = null;
  $scope.image = null;
  $scope.image_url = null;

  $scope.$on('item', function (event, arg) {
    $scope.changeItem(arg);
  });

  $scope.hasItem = function() {
    return $scope.item != null;
  }

  $scope.hasImage = function() {
    return $scope.image != null;
  }

  $scope.hasProperties = function() {
    return $scope.hasItem() && $scope.item.properties.length > 0;
  }

  $scope.hasReferences = function() {
    return $scope.hasItem() && $scope.item.xrefs.length > 0;
  }

  $scope.changeItem = function(ref) {
    if (ref != null && ref != "") {
      console.log('Fetch item', ref);
      $http.get("/kb/item?fmt=cjson&id=" + ref).then(function(results) {
        var item = results.data;
        $scope.item = item;
        $scope.image = null;
        $scope.image_url = null;
        if (item.thumbnail) {
          $scope.changeImage(item.thumbnail);
        }
      });
    }
  }

  $scope.changeImage = function(thumbnail) {
    var thumb_url = commons_service + thumbnail.replace(' ', '+');
    $http.jsonp(thumb_url).then(function(results) {
      var imginfo = results.data;
      for (var p in imginfo.query.pages) {
        var page = imginfo.query.pages[p];
        var thumburl = page.imageinfo[0].thumburl;
        $scope.image = thumburl;
        $scope.image_url = commons_url + thumbnail;
      }
    }, function error(response) {
      console.log("error getting thumbnail", response);
      $scope.image = null;
      $scope.image_url = null;
    });
  }
})

