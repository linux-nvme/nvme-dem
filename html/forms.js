$("form").submit(function(event) {
   var fields = $( ":input" ).serializeArray();
   sessionStorage.setItem("dem_addr", fields[0].value);
   sessionStorage.setItem("dem_port", fields[1].value);

   var auth = make_basic_auth(fields[2].value, fields[3].value);
   sessionStorage.setItem("dem_auth", auth);

  checkAddress();

  event.preventDefault();
});

/* When the user clicks on the button,
toggle between hiding and showing the dropdown content */
function myFunction(id) {
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
