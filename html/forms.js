$("#loginForm").submit(function(event) {
  var fields = $( ":input" ).serializeArray();
  sessionStorage.setItem("dem_addr", fields[0].value);
  sessionStorage.setItem("dem_port", fields[1].value);

  var auth = make_basic_auth(fields[2].value, fields[3].value);
  sessionStorage.setItem("dem_auth", auth);

  checkAddress();
});

function updateForm() {
  var newuser, newpswd1, newpswd2;
  var olduser, oldpswd;

  $("#updateError").html("");

  olduser = $("#olduser").val().trim();
  if (olduser == "") {
    $("#updateError").html("All fields are manditory");
    $(":input#olduser").focus();
    return 1;
  }

  oldpswd = $("#oldpswd").val().trim();
  if (oldpswd == "") {
    $("#updateError").html("All fields are manditory");
    $("#oldpswd").focus();
    return 1;
  }

  newuser = $("#newuser").val().trim();
  if (newuser == "") {
    $("#updateError").html("All fields are manditory");
    $("#newuser").focus();
    return 1;
  }

  newpswd1 = $("#newpswd1").val().trim();
  if (newpswd1 == "") {
    $("#updateError").html("All fields are manditory");
    $("#newpswd1").focus();
    return 1;
  }

  newpswd2 = $("#newpswd2").val().trim();
  if (newpswd2 == "") {
    $("#updateError").html("All fields are manditory");
    $("#newpswd2").focus();
    return 1;
  }

  if (newpswd1 != newpswd2) {
    $("#updateError").html("New Passwords does not match");
    $("#newpswd1").val("");
    $("#newpswd2").val("");
    $("#newpswd1").focus();
    return 1;
  }

  var oldauth = make_basic_auth(olduser, oldpswd);
  var newauth = make_basic_auth(newuser, newpswd1);

  if (sessionStorage.getItem("dem_auth") != oldauth) {
    $("#updateError").html("Invalid user or password");
    $("#oldpswd").val("");
    $("#olduser").focus();
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

  $("#olduser").val("");
  $("#oldpswd").val("");
  $("#newuser").val("");
  $("#newpswd1").val("");
  $("#newpswd2").val("");

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
