$("#loginForm").submit(function(event) {
  var fields = $( ":input" ).serializeArray();
  sessionStorage.setItem("dem_addr", fields[0].value);
  sessionStorage.setItem("dem_port", fields[1].value);

  var auth = make_basic_auth(fields[2].value, fields[3].value);
  sessionStorage.setItem("dem_auth", auth);

  checkAddress();
});

function updateForm() {
  var fields = $( ":input" ).serializeArray();

  $("#updateError").html("");

  for(i = 4; i < 9; i++) {
    if (fields[i].value.trim() == "") {
      $("#updateError").html("All fields are manditory");
      $("input[name='" + fields[i].name + "']").focus();
      return 1;
    }
  }

  if (fields[7].value != fields[8].value) {
    $("#updateError").html("New Passwords does not match");
    $("input[name='newpswd1']").html("");
    $("input[name='newpswd2']").html("");
    $("input[name='newpswd1']").focus();
    return 1;
  }

  var oldauth = make_basic_auth(fields[4].value, fields[5].value);
  var newauth = make_basic_auth(fields[6].value, fields[7].value);

  if (sessionStorage.getItem("dem_auth") != oldauth) {
    $("#updateError").html("Invalid user or password");
    $("input[name='oldpswd']").html("");
    $("input[name='olduser']").focus();
    return 1;
  }

  $("#editVerb").html("POST");
  $("#editUri").html("dem/signature");
  $("#objectType").html("dem");
  $("#parentUri").html("reset");
  $("#objectValue").html(newauth.substring(6));

  if (!sendRequest()) {
    $("#updateError").html("Failed to send update");
    return 1;
  }

  $("input[name='oldpswd']").html("");
  $("input[name='newuser']").html("");
  $("input[name='newpswd1']").html("");
  $("input[name='newpswd1']").html("");

  return 0;
}

function OK() {
  return 0;
}

/* When the user clicks on the button,
toggle between hiding and showing the dropdown content */
function showDropdown(id) {
  var dropdowns = document.getElementsByClassName("dropdown-content");
  var i;
  for (i = 0; i < dropdowns.length; i++) {
    var openDropdown = dropdowns[i];
    if (openDropdown.classList.contains('show')) {
      openDropdown.classList.remove('show');
    }
  }
  document.getElementById(id).classList.toggle("show");
}

// Close the dropdown if the user clicks outside of it
window.onclick = function(event) {
  var isDropButton = false;
  if (event.target.matches == undefined)
    isDropButton = event.target.className == "dropbtn";
  else
    isDropButton = event.target.matches('.dropbtn');
  if (!isDropButton) {
    var dropdowns = document.getElementsByClassName("dropdown-content");
    var i;
    for (i = 0; i < dropdowns.length; i++) {
      var openDropdown = dropdowns[i];
      if (openDropdown.classList.contains('show')) {
        openDropdown.classList.remove('show');
      }
    }
  }
}
