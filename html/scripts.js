function make_basic_auth(user, password) {
  var tok = user + ':' + password;
  var hash = Base64.encode(tok);
  return "Basic " + hash;
}

function showContacts() {
  $("#contactPage").show();
  $("#contentPage").hide();
  $("#listPage").hide();
  $("#detailPage").hide();
  $("#objectType").html("");
  $("#objectValue").html("");
}

function showContents(page) {
  $("#listPage").html("");
  $("#detailPage").html("");
  $("#contentPage").html("");
  $("#contactPage").hide();
  $("#listPage").hide();
  $("#detailPage").hide();
  $("#objectType").html(page);
  $("#objectValue").html("");
  loadDoc(page);
}

function showList(page) {
  if (page != undefined)
    $("#listPage").html("");
  $("#detailPage").html("");
  $("#listPage").show();
  $("#contactPage").hide();
  $("#contentPage").hide();
  $("#detailPage").hide();
  $("#objectValue").html("");
  $("#args").html("");
  $("#parentargs").html("");
  if (page != undefined) {
    $("#objectType").html(page);
    loadDoc(page);
  }
}

function showDetails(page) {
  $("#detailPage").html("");
  $("#detailPage").show();
  $("#contactPage").hide();
  $("#contentPage").hide();
  $("#listPage").hide();
  if (page != undefined) {
    $("#objectValue").html(page);
    loadDoc(page);
  }
}

function clearSession() {
  sessionStorage.removeItem("dem_addr");
  sessionStorage.removeItem("dem_port");
  sessionStorage.removeItem("dem_auth");
  $("#addrForm").show();
  $("#contactPage").hide();
  $("#contentPage").hide();
  $("#listPage").hide();
  $("#detailPage").hide();
  $("#menu").hide();
  $("#first").focus();
}
function Capitalize(str) {
  return str.charAt(0).toUpperCase() + str.slice(1);
}
function loadDel(sub, item, val) {
  var typ = $("#objectType").html();
  var obj = $("#objectValue").html();
  var str = "delete " + Capitalize(typ);
  var uri = typ;

  if (obj == "") {
    uri += "/" + sub;
    str += " '" + sub + "'";
  } else {
    uri += "/" + obj;
    str += " '" + obj + "'";

    if (item != undefined && typeof(item) == typeof(str)) {
      if (typ == "target") {
        uri += "/subsystem";
        str += " Subsystem '" + sub + "'";
      } else if (typ == "group") {
        str += " " + Capitalize(sub) + " '" + item + "'";
      } else {
         str += "  '" + sub + "'";
      }
    } else {
      if (sub == "/portid")
        str += " Port ID " + item;
      else if (sub == "/transport")
        str += " Transport " + item;
      else if (sub == "/interface")
        str += " Interface " + item;
      else if (typ == "target") {
        uri += "/subsystem";
        str += " Subsystem '" + sub + "'";
      }
    }

    if (sub != "" && sub.charAt(0) != "/")
      uri += "/";

    uri += sub;

    if (val != undefined && typ == "target") {
      if (item == "/host") {
        uri += item + "/" + val;
        str += " Host '" + val + "' from the Allowed Hosts list";
      } else if (item == "/nsid") {
        uri += item + "/" + val;
        str += " NS ID " + val;
      }
    } else if (item != undefined)
      uri += "/" + item;
  }

  openDialog("Are you sure you want to " + str, "DELETE", uri);
}

function loadAltDel(alias, nqn, host) {
  var str;
  var uri = "target/" + alias + "/subsystem/" + nqn + "/host/" + host;

  str = "Are you sure you want to delete Host '" + host + "'" +
        " from Allowed list on Subsytem '" + nqn + "'" +
        " on Target '" + alias + "'?";

  openDialog(str, "DELETE", uri);
}

function toggleMode() {
  $("#res_refresh").hide();
  $("#log_refresh").hide();
  $("#oob_data").hide();
  $("#oob_note").hide();
  $("#ib_note").hide();
  $("#event_note").hide();
  $("#poll_note").hide();
  if ($("#mode").val() == "polled") {
    $("#poll_note").show();
    $("#log_refresh").show();
  } else if ($("#mode").val() == "oob") {
    $("#oob_data").show();
    $("#oob_note").show();
    $("#res_refresh").show();
  } else if ($("#mode").val() == "ib") {
    $("#ib_note").show();
    $("#res_refresh").show();
  }
}

function formTargetAlias(obj) {
  var str = "<p class='header'>";
  var hidden = " style='display:none'";
  var selected = " selected";
  var args = $("#parentargs").html().trim().split(",");
  var mode = "";
  var fam = "";
  var adr = "";
  var port = "";
  var refresh = "";

  if (obj == undefined) obj = "";
  if (obj == "")
    str += "Add a Target</p>";
  else {
    str += "Update Target '" + $("#objectValue").html() + "'</p>";
    if (args[0] != undefined) refresh = args[0];
    if (args[1] != undefined) mode = args[1];
    if (args[2] != undefined) fam = args[2];
    if (args[3] != undefined) adr = args[3];
    if (args[4] != undefined) port = args[4];
  }
  str += "<p>Alias: <input id='alias' type='text' value='" + obj +
             "'></input></p>";

  str += "<p>Management Mode<select id='mode' onchange='toggleMode()'>";
  str += "<option value='polled'";
  if (mode == "LocalMgmt") str += selected;
  str += ">Locally</option>"
  str += "<option value='oob'";
  if (mode == "OutOfBandMgmt") str += selected;
  str += ">Out of Band</option>"
  str += "<option value='ib'";
  if (mode == "InBandMgmt") str += selected;
  str += ">In Band</option>"

  str += "</select><span class='units'>";

  visible = (mode == "InBandMgmt") ? "" : hidden;
  str += "<div id='ib_note'" + visible +
         ">(use NVMe-of primatives to configure target)</div>";
  visible = (mode == "OutOfBandMgmt") ? "" : hidden;
  str += "<div id='oob_note'" + visible +
         ">(use restful interface to configure target)</div>";
  visible = (mode == "LocalMgmt") ? "" : hidden;
  str += "<div id='poll_note'" + visible +
         ">(poll target logpages periodically for resource changes)</div>" +
         "</span></p>";

  visible = (mode == "OutOfBandMgmt") ? "" : hidden;
  str += "<p><div id='oob_data'" + visible + ">";

  str += formFamily(fam);
  str += "<p>Address: <input id='adr' type='text'" +
         " value='" + adr + "'></input></p>";
  str += "<p>Restful Port: <input id='svc' type='number' min='0'" +
         " value='" + port + "'></input></p>";
  str += "</div></p>";

  str += "<p>Refresh: " +
         "<input id='refresh' type='number' value='" + refresh + "' min='0'>" +
         "</input><span class='units'>minutes - 0 disables timer ";
  visible = (mode != "OutOfBandMgmt") ? "" : hidden;
  str += "<span id='log_refresh'" + visible + ">Log Page</span>"
  visible = (mode == "OutOfBandMgmt") ? "" : hidden;
  str += "<span id='res_refresh'" + visible + ">Resource info</span>"
  str += " refreshing</span></p>";
  return str;
}
function formHostAlias(obj) {
  var nqn = $("#args").html().trim();
  var str = "<p class='header'>";
  if (obj == undefined) obj = "";

  if (obj == "")
    str += "Add a Host";
  else
    str += "Update Host '" + $("#objectValue").html() + "'";

  str += "</p><p>Alias: <input id='alias' type='text' value='" + obj +
             "'></input></p>";
  str += "<p>Host NQN: <input id='hostnqn' type='text' value='" + nqn +
             "'></input></p>";
  return str;
}
function formGroupHost() {
  var str = "";
  var xhttp = new XMLHttpRequest();
  var auth = sessionStorage.dem_auth;
  var url = "http://";

  url += sessionStorage.dem_addr + ":" + sessionStorage.dem_port + "/host";

  xhttp.open("GET", url, false);
  xhttp.setRequestHeader("Content-type", "text/plain; charset=UTF-8");
  xhttp.setRequestHeader("Access-Control-Allow-Origin", "*");
  xhttp.setRequestHeader('Authorization', auth);
  xhttp.send();

  var obj = jQuery.parseJSON(xhttp.responseText);
  var list = obj["Hosts"].sort();
  var i;

  str += "<p>Add a Host Alias to Group '" + $("#objectValue").html() + "'";
  str += "</p><br>";
  str += "<p>Alias: <select id='alias'>";

  for (i = 0; i < list.length; i++)
    str += "<option>" + list[i] + "</option>";

  str += "</select></p>";
  return str;
}
function formGroupTarget() {
  var str = "";
  var xhttp = new XMLHttpRequest();
  var auth = sessionStorage.dem_auth;
  var url = "http://";

  url += sessionStorage.dem_addr + ":" + sessionStorage.dem_port + "/target";

  xhttp.open("GET", url, false);
  xhttp.setRequestHeader("Content-type", "text/plain; charset=UTF-8");
  xhttp.setRequestHeader("Access-Control-Allow-Origin", "*");
  xhttp.setRequestHeader('Authorization', auth);
  xhttp.send();

  var obj = jQuery.parseJSON(xhttp.responseText);
  var list = obj["Targets"].sort();
  var i;

  str += "Add a Target Alias to Group '" + $("#objectValue").html() + "'";
  str += "</p>";
  str += "<p>Alias: <select id='alias'>";

  for (i = 0; i < list.length; i++)
    str += "<option>" + list[i] + "</option>";

  str += "</select></p>";
  return str;
}
function formAllowedHost(ss) {
  var str = "<p class='header'>";
  var xhttp = new XMLHttpRequest();
  var auth = sessionStorage.dem_auth;
  var url = "http://";

  url += sessionStorage.dem_addr + ":" + sessionStorage.dem_port + "/host";

  xhttp.open("GET", url, false);
  xhttp.setRequestHeader("Content-type", "text/plain; charset=UTF-8");
  xhttp.setRequestHeader("Access-Control-Allow-Origin", "*");
  xhttp.setRequestHeader('Authorization', auth);
  xhttp.send();

  var obj = jQuery.parseJSON(xhttp.responseText);
  var list = obj["Hosts"].sort();
  var i;

  str += "Add a Host to the Allowed Host list of Target '" +
         $("#objectValue").html() + "' Subsystem '" + ss + "'";

  str += "<p>Alias: <select id='alias'>";

  for (i = 0; i < list.length; i++) {
    str += "<option";
    if (list[i] == obj) str+= " selected";
    str += ">" + list[i] + "</option>";
  }
  str += "</select></p>";
  return str;
}
function formGroup(obj) {
  var str = "<p class='header'>";
  if (obj == undefined) obj = "";
  if (obj == "")
    str += "Add a Group"
  else
    str += "Update Group '" + obj + "'";

  str += "<p>Name: <input id='group' type='text' value='" + obj +
         "'></input></p>";
  return str;
}
function formSubsystem(obj) {
  var str = "<p class='header'>";
  if (obj == undefined) obj = "";
  if (obj == "")
    str += "Add a Subsystem to "
  else
    str += "Update Subsystem '" + obj + "' on ";

  str += "Target '" + $("#objectValue").html() + "'";

  str += "<p>Subsystem NQN: <input id='subnqn' type='text'" +
         " value='" + obj + "'></input></p>";
  str += "<p>Allow Any Host: <input id='allowany' type='checkbox'"
  if ($("#args").html() == "true")
    str += " checked";
  str += "></input></p>";
  return str;
}
function formType(typ) {
  var str = "";
  var selected = " selected";

  str += "<p>Type: <select id='typ'><option value=''></option>";
  str += "<option value='rdma'";
  if (typ == "rdma") str += selected;
  str += ">rdma</option><option value='fc'";
  if (typ == "fc") str += selected;
  str += ">fc</option><option value='tcp'";
  if (typ == "tcp") str += selected;
  str += ">tcp</option></select></p>";

  return str;
}
function formFamily(fam) {
  var str = "";
  var selected = " selected";

  str += "<p>Family: <select id='fam'><option value=''></option>";
  str += "<option value='ipv4'";
  if (fam == "ipv4") str += selected;
  str += ">ipv4</option><option value='ipv6'";
  if (fam == "ipv6") str += selected;
  str += ">ipv6</option><option value='fc'";
  if (fam == "fc") str += selected;
  str += ">fc</option></select></p>";

  return str;
}

function formPortID(sub) {
  var str = "<p class='header'>";
  var args = $("#args").html().trim().split(" ");
  var typ = args[0];
  var fam = args[1];
  var adr = args[2];
  var svc = args[3];

  if (sub == undefined) {
    sub = ""; typ = ""; fam = ""; adr = ""; svc = "";
  }

  if (sub == "")
    str += "Add a Port to "
  else
    str += "Update Port ID " + sub + " on ";
  str += " Target '" + $("#objectValue").html() + "'";

  str += "<p>Port ID: <input id='portid' type='number' min='1'" +
         " value='" + sub + "'></input></p>";
  str += formType(typ);
  str += formFamily(fam);
  str += "<p>Address: <input id='adr' type='text'" +
         " value='" + adr + "'></input></p>";
  str += "<p>Service ID: <input id='svc' type='number' min='0'" +
         " value='" + svc + "'></input></p>";
  return str;
}
function formTransport(sub) {
  var str = "<p class='header'>";
  var args = $("#args").html().trim().split(" ");
  var typ = args[0];
  var fam = args[1];
  var adr = args[2];

  if (sub == undefined) {
    sub = ""; typ = ""; fam = ""; adr = "";
    str += "Add a Transport to "
  } else
    str += "Update Transport ID " + sub + " on ";

  str += " Host '" + $("#objectValue").html() + "'";

  str += "<p>Transport ID: <input id='portid' type='number'" +
         " value='" + sub + "'></input></p>";
  str += formType(typ);
  str += formFamily(fam);
  str += "<p>Address: <input id='adr' type='text'" +
         " value='" + adr + "'></input></p>";
  return str;
}
function formNSID(sub,val) {
  var str = "<p class='header'>";
  var args = $("#args").html().trim().split(",");
  var devid = "";
  var devnsid = "";
  if (val == undefined)
    val = "";
  else {
    if (args[0] != undefined) devid = args[0];
    if (args[1] != undefined) devnsid = args[1];
  }
  if (val == "") {
    str += "Add a Namespace to ";
    val = "1";
    devid = "0";
    devnsid = "1";
  } else
    str += "Update Namespace ID " + val + " on ";
  str += " Target '" + $("#objectValue").html() + "'";
  str += " Subsystem '" + sub + "'";

  str += "<p>NS ID: <input id='nsid' type='number'" +
         " value='" + val + "' min='1'></input></p>";
  str += "<p>Device ID: <input id='devid' type='number'" +
         " value='" + devid + "' min='-1'></input>" +
         "<span class='units'><font class='hilite1'>" +
         "X</font> of /dev/nvme<font class='hilite1'>X</font>nY " +
         " or -1 for /dev/nullb0</span></p>";
  str += "<p>Device NS ID: <input id='devnsid' type='number'" +
         " value='" + devnsid + "'min='1'></input>" +
         "<span class='units'><font class='hilite2'>Y</font> of " +
         "/dev/nvmeXn<font class='hilite2'>Y</font> - ignored for /dev/nullb0";
  return str;
}

function buildFilterMenu() {
  var str = "";
  var checked = ' checked="checked"';
  var filter = 0;
  var cur = $("#filter").html();
  if (cur.indexOf("=rdma") != -1) filter = 1;
  else if (cur.indexOf("=tcp") != -1) filter = 2;
  else if (cur.indexOf("=fc") != -1) filter = 3;
  else if (cur.indexOf("=Out") != -1) filter = 4;
  else if (cur.indexOf("=In") != -1) filter = 5;
  else if (cur.indexOf("=Loc") != -1) filter = 6;

  str += "<span style='float:right'>";
  str += '<img src="filtered.png" alt="filter" class="dropbtn"';
  if (filter == 0) str += ' style="display:none"';
  str += ' id="filteredmenu" onclick="showDropdown(' + "'Filter'" + ');">';
  str += '<img src="unfiltered.png" alt="filter" class="dropbtn"';
  if (filter != 0) str += ' style="display:none"';
  str += ' id="unfilteredmenu" onclick="showDropdown(' + "'Filter'" + ');">';
  str += '<div id="Filter" class="dropdown-content">';
  str += '<label class="filter">No Filter' +
         '<input type="radio" name="radio" onchange="filter(0)"';
  if (filter == 0) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only RDMA Fabric' +
         '<input type="radio" name="radio" onchange="filter(1)"';
  if (filter == 1) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only TCP Fabric' +
         '<input type="radio" name="radio" onchange="filter(2)"';
  if (filter == 2) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only FC Fabric' +
         '<input type="radio" name="radio" onchange="filter(3)"';
  if (filter == 3) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only Out-of-Band Managed' +
         '<input type="radio" name="radio" onchange="filter(4)"';
  if (filter == 4) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only In-Band Managed' +
         '<input type="radio" name="radio" onchange="filter(5)"';
  if (filter == 5) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += '<label class="filter">Only Locally Managed' +
         '<input type="radio" name="radio" onchange="filter(6)"';
  if (filter == 6) str += checked;
  str += '><span class="checkmark"></span></label>';
  str += "</div></span>";

  return str;
}

function filter(id) {
  var old = $("#filter").html();
  switch (id) {
    case 1: $("#filter").html("?fabric=rdma"); break;
    case 2: $("#filter").html("?fabric=tcp"); break;
    case 3: $("#filter").html("?fabric=fc"); break;
    case 4: $("#filter").html("?mode=OutOfBandMgmt"); break;
    case 5: $("#filter").html("?mode=InBandMgmt"); break;
    case 6: $("#filter").html("?mode=LocalMgmt"); break;
    default: $("#filter").html(""); break;
  }
  if (id == 0) {
    $("#unfilteredmenu").show();
    $("#filteredmenu").hide();
  } else {
    $("#filteredmenu").show();
    $("#unfilteredmenu").hide();
  }
  if (old != $("#filter").html())
    loadDoc($("#objectType").html());
}
function loadEdit(obj, sub, val) {
  var typ = $("#objectType").html();
  var item = $("#objectValue").html();
  var str = "";
  var uri = typ;

  if (obj == undefined) obj = "";

  if (item != "")
    uri += "/" + item;

  if (obj != "" && obj.charAt(0) != "/") {
    if (typ == "target") {
      uri += "/subsystem/";
      if (val == undefined)
        str += formSubsystem(obj);
    } else
       uri += "/";
  }

  uri += obj;

  if (obj == "") {
    if (typ == "target")
      str += formTargetAlias(item);
    else if (typ == "host")
      str += formHostAlias(item);
    else
      str += formGroup(item, sub);
  } else {
    if (sub != undefined && typeof(sub) == typeof(str)) {
      if (sub.charAt(0) != "/")
         str += "  '" + sub + "'";
    } else {
      if (typ == "target") {
        if (typeof(sub) == typeof(0) && val == undefined)
          str += formPortID(sub);
      } else {
        if (typeof(sub) == typeof(0) && val == undefined)
          str += formTransport(sub);
      }
    }

    if (sub != undefined) {
      if (typeof(sub) == typeof(str)) {
        if (sub.charAt(0) != "/")
          uri += "/" + sub;
        else if (typ == "target") {
          uri += sub;
          if (val == undefined) {
            if (sub == "/ndis")
              str += formNSID(obj);
            else if (sub == "/host")
              str += formAllowedHost(obj);
          }
        }
      } else
        uri += "/" + sub;
    }

    if (val != undefined) {
      if (typeof(val) == typeof(str))
           str += "  '" + val + "'";
      else {
        if (typ == "target") {
          str += formNSID(obj,val);
        } else {
          str += " - " + val;
        }
      }
      uri += "/" + val;
    }
  }

  openDialog(str, "PUT", uri);
}

function loadAdd(sub, val) {
  var typ = $("#objectType").html();
  var obj = $("#objectValue").html();
  var str = "";
  var uri = typ;

  if (sub == undefined) {
    if (typ == "target")
      str += formTargetAlias();
    else if (typ == "host")
      str += formHostAlias();
    else
      str += formGroup();
  } else if (typ == "target") {
    uri += "/" + obj;
    if (sub == "/subsystem") {
      uri += sub;
      str += formSubsystem();
    } else if (sub == "/portid") {
      uri += sub;
      if (val == undefined)
        str += formPortID();
    } else if (sub == "/interface") {
      uri += sub;
      if (val == undefined)
        str += formTransport();
    } else if (sub == "/transport") {
      uri += sub;
      if (val == undefined)
        str += formTransport();
    }
  } else if (typ == "group") {
    uri += "/" + obj + "/" + sub;
    if (val == undefined) {
      if (sub == "host")
        str += formGroupHost();
      else
        str += formGroupTarget();
    }
  } else if (typ == "host") {
    uri += "/" + obj;
    if (sub == "/transport") {
      uri += sub;
      if (val == undefined)
        str += formTransport();
    } else if (sub == "/interface") {
      uri += sub;
      if (val == undefined)
        str += formTransport();
    }
  }

  if (val != undefined) {
    if (val == "/host") {
      uri += "/subsystem/" + sub;
      str += formAllowedHost(sub);
    } else if (val == "/nsid") {
      uri += "/subsystem/" + sub;
      str += formNSID(sub);
    } else
      uri += "/";
    uri += val;
  }
  $("#args").html("");

  openDialog(str, "PUT", uri);
}

function parseTransports(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]);
  var fam = "";
  var typ = "";
  var adr = "";
  var str = "";
  var ref = "";

  listC.forEach(function(itemC) {
    var listD = obj[itemA][itemB][itemC];
    if (itemC == "TRTYPE")
      typ = " " + listD;
    else if (itemC == "ADRFAM")
      fam = " " + listD;
    else if (itemC == "TRADDR")
      adr = " " + listD;
    });

  ref += itemB;

  str += '<p class="data">';
  str += itemB + ": " + typ + fam + adr + " &nbsp; ";
  str += '<img src="pencil.png" alt="get" class="icon" onclick="saveVal(';
  str += "'" + typ + fam + adr + "'" + "); loadEdit(";
  str += "'/interface'," + ref + ')">&nbsp; ';
  str += '<img src="trash.png" alt="del" class="icon" onclick="loadDel(';
  str += "'/interface'," + ref + ')"></p>';

  return str;
}

function parseMgmtMode(obj, itemA) {
  var str = "";
  var args = "";
  var mode = obj[itemA];

  str += '<p class="data">Management Mode: ';
  if (mode == "OutOfBandMgmt")
    str += "Out-of-Band";
  else if (mode == "InBandMgmt")
    str += "In-Band";
  else if (mode == "LocalMgmt")
    str += "Local";
  else
    str += mode;

  str += "</p>";

  args = $("#parentargs").html();
  $("#parentargs").html(args + "," + mode);

  return str;
}

function parseInterface(obj, itemA) {
  var listB = Object.keys(obj[itemA]);
  var fam = "";
  var adr = "";
  var port = "";
  var str = "";
  var args = $("#parentargs").html();

  if (args.indexOf("OutOfBand") == -1)
    return str;

  listB.forEach(function(itemB) {
    var listC = obj[itemA][itemB];
    if (itemB == "FAMILY")
      fam = listC;
    else if (itemB == "ADDRESS")
      adr = listC;
    else if (itemB == "PORT")
      port = listC;
    });
  if (fam != "") {
    str += '<p class="data">';
    str += itemA + ": " + fam;
    if (adr != "") str += " " + adr;
    if (port != "") str += ":" + port;
    str += "</p>" ;
  }

  args += "," + fam + "," + adr + "," + port;
  $("#parentargs").html(args);

  return str;
}

function parseInterfaces(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]);
  var fam = "";
  var typ = "";
  var adr = "";
  var svc = "";
  var str = "";
  listC.forEach(function(itemC) {
    var listD = obj[itemA][itemB][itemC];
    if (itemC == "TRTYPE")
      typ = " " + listD;
    else if (itemC == "ADRFAM")
      fam = " " + listD;
    else if (itemC == "TRADDR")
      adr = " " + listD;
    else if (itemC == "TRSVCID")
      svc = ":" + listD;
    });
  str += '<p class="data">';
  str += itemB + ":&nbsp; " + typ + fam + adr + svc + "</p>" ;

  return str;
}

function parseSubsystems(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]).sort();
  var nqn = "";
  var allowany = false;
  var nsids = undefined;
  var hosts = undefined;
  var str = "";
  var ref = "";
  listC.forEach(function(itemC) {
      var listD = obj[itemA][itemB][itemC];
      if (itemC == "SUBNQN")
        nqn = listD;
      else if (itemC == "AllowAnyHost")
        allowany = !!listD;
      else if (itemC == "NSIDs")
        nsids = listD;
      else if (itemC == "Hosts")
        hosts = listD;
    });

  ref = "'" + nqn + "'";

  str += '<h5>Subsystem NQN: ' + nqn + " &nbsp; " ;
  str += '<span class="subtext">';
  if (allowany)
      str += "(Allow Any Host) &nbsp; " ;
  else
      str += "(Restricted to 'Allowed Hosts') &nbsp; " ;
  str += '<img src="pencil.png" alt="get" class="icon" onclick="saveVal(';
  str += "'" + allowany + "'" + '); loadEdit(' + ref + ')">&nbsp; ';
  str += '<img src="trash.png" alt="del" class="icon" onclick="loadDel(';
  str += ref + ')">';
  str += '</span></h5>' ;

  str += '<div class="details"><br><h5>NSIDs &nbsp;';
  str += '<img src="plus.png" alt="add" class="icon" onclick="loadAdd('
  str += ref + ",'/nsid')" + '"></h5>';
  if (nsids != undefined) {
    str += '<div class="subdetails">';
    nsids.forEach(function(nsid){
      var listD = Object.keys(nsid);
      var id = 0;
      var devid = 0;
      var devns = 0;
      listD.forEach(function(itemD) {
        if (itemD == "NSID")
          id = nsid[itemD];
        else if (itemD == "DeviceID")
          devid = nsid[itemD];
        else if (itemD == "DeviceNSID")
          devns = nsid[itemD];
      })
      str += '<p>' + id + ":&nbsp; Device: ID " + devid + " &nbsp;";
      if (devid != -1)
	 str += "NSID " + devns + " &nbsp;";
      str += ' <img src="pencil.png" alt="get" class="icon" ';
      str += 'onclick="saveVal(' + "'" + devid + ',' + devns + "'" + ');';
      str += 'loadEdit(' + ref + ",'/nsid'," + id  + ')">&nbsp; ';
      str += '<img src="trash.png" alt="del" class="icon" onclick="loadDel(';
      str += ref + ",'/nsid'," + id + ')">';
      str += '</p>';
    });
    str += '</div>';
  } else
    str += '<br>';

  if (!allowany) {
    str += "<h5>Allowed Hosts &nbsp;";
    str += '<img src="plus.png" alt="add" class="icon" onclick="loadAdd('
    str += ref + ",'/host')" + '"></h5>';
    if (hosts != undefined) {
      str += '<div class="subdetails">';
      hosts.forEach(function(host){
        str += "<p>" + host + "&nbsp; ";
        str += '<img src="trash.png" alt="del" class="icon" ';
        str += 'onclick="loadDel(' + ref + ",'/host','" + host + "'" + ')">';
        str += "</p>";
      });
      str += '</div>';
    }
  }

  str += "</div><br>";
  return str;
}

function parseACL(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]);
  var str = "";
  var alias = "";
  var nqn = "";
  var ref = "";
  listC.forEach(function(itemC){
    if (itemC == "Alias")
      alias = obj[itemA][itemB][itemC];
    else if (itemC == "SUBNQN")
      nqn = obj[itemA][itemB][itemC]
  });

  str += '<p class="data">';
  str += "Target '" + alias + "' Subsystem '" + nqn + "'";

  if (itemA == "Restricted") {
    ref = "'" + alias + "','" + nqn + "','" + $("#objectValue").html() + "'";
    str += ' &nbsp; <img src="trash.png" alt="del" class="icon"'
    str += ' onclick="loadAltDel(' + ref + ')">&nbsp; ';
  }

  str += '</p>';
  return str;
}

function parsePortIDs(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]);
  var str = "";
  var id = 0;
  var typ = "";
  var fam = "";
  var adr = "";
  var svc = "";
  var ref = "";
  listC.forEach(function(itemC){
    if (itemC == "PORTID")
      id = obj[itemA][itemB][itemC];
    else if (itemC == "TRTYPE")
      typ = obj[itemA][itemB][itemC]
    else if (itemC == "ADRFAM")
      fam = obj[itemA][itemB][itemC]
    else if (itemC == "TRADDR")
      adr = obj[itemA][itemB][itemC]
    else if (itemC == "TRSVCID")
      svc = obj[itemA][itemB][itemC]
  });

  ref += "'/portid'," + id;

  str += '<p style="margin-left:-40px">' + id + ":&nbsp; " + typ;
  if (typ == "fc")
    str += " " + adr + " service id " + svc;
  else
    str += " " + fam + " " + adr + ":" + svc;
  str += '&nbsp; <img src="pencil.png" alt="get" class="icon" ';
  str += 'onclick="saveVal(' + "'" + typ + " " + fam + " ";
  str += adr + " " + svc + "'" + ');loadEdit(' + ref + ')">';
  str += '&nbsp; <img src="trash.png" alt="del" class="icon" ';
  str += 'onclick="loadDel(' + ref + ')"></p>';
  return str;
}

function parseNSDevs(obj, itemA, itemB) {
  var listC = Object.keys(obj[itemA][itemB]);
  var str = "";
  var id = undefined;
  var devid = "";
  var devns = "";
  var nsdev = undefined;
  listC.forEach(function(itemC){
    if (itemC == "NSID")
      id = obj[itemA][itemB][itemC] + ":&nbsp; ";
    else if (itemC == "DeviceID")      
      devid = obj[itemA][itemB][itemC]
    else if (itemC == "DeviceNSID")
      devns = " &nbsp; NSID " + obj[itemA][itemB][itemC]
    else if (itemC == "NSDEV")
      nsdev = " " + obj[itemA][itemB][itemC]
  });

  str += '<p class="data">';

  if (devid == "-1")
    devns = "";

  devid = "Device:&nbsp; ID " + devid;

  if (nsdev != undefined)
    str += nsdev;
  if (id != undefined)
    str += id + devid + devns;
  else
    str += itemB + ":&nbsp;" + devid + devns;

  str += "</p>";

  return str;
}

function parseObject(obj, itemA) {
  var listB = Object.keys(obj[itemA]);
  var str = "";
  var closeDiv = false;
  var typ = $("#objectType").html();

  if (itemA == "Interfaces" && typ == "dem") {
    str += "<h1>About</h1>";
    str += '<div style="padding-left:1.6em">';
    str += "<p>Use the menus to the right to view the defined objects ";
    str += "managed by this DEM.</p>";
    str += "<p>Currently active DEM fabric interfaces for NVMe-oF Hosts ";
    str += "to query are listed below.</p></div>";
  }

  str += "<h1>";
  if (itemA == "NSDevs")
    str += "NS Devices";
  else if (itemA == "PortIDs")
    str += " Port IDs";
  else
    str += itemA;

  tmp = itemA.substr(0,itemA.length-1).toLowerCase();

  if ((itemA == "Interfaces") || (itemA == "NSDevices") ||
      (itemA == "Shared") || (itemA == "Restricted"))
    str += "</h1>";
  else {
    closeDiv = true;
    str += " &nbsp;";
    str += '<img src="plus.png" alt="add" class="icon" onclick="loadAdd(';
    if (itemA == "Targets" || itemA == "Hosts" || itemA == "Groups" ) {
      if (typ == "group" && tmp != "group")
        str += "'" + tmp + "'";
      str += ')">';
      if (itemA == "Targets" && typ == "target")
        str += buildFilterMenu();
      str += '</h1>';
      if (listB.length > 22)
        str += '<div class="three-columns">';
      else if (listB.length > 11)
        str += '<div class="two-columns">';
      else
        str += '<div class="one-column">';
    } else {
      str += "'/" + tmp + "'" + ')"></h1>';
      if (itemA == "PortIDs")
        str += '<div class="details">';
    }
  }

  listB.sort(basicSort(obj[itemA]));

  listB.forEach(function(itemB) {
    var listC = Object.keys(obj[itemA][itemB]);
    var name = "";
    if (typeof(obj[itemA][itemB]) == typeof("") && listC != undefined) {
      name = obj[itemA][itemB];
      str += '<h3>';
      if (typ != "group" || tmp == "group") {
        str += '<img src="page.png" alt="get" class="icon" onclick="';
        str += "showDetails('" + name + "'" + ')">&nbsp; ';
        str += '<img src="trash.png" alt="del" class="icon" onclick="loadDel(';
        str += "'" + name + "'" + ')">&nbsp; ';
      } else {
        str += '<img src="trash.png" alt="del" class="icon" onclick="loadDel(';
        str += "'" + tmp + "','" + name + "'" + ')">&nbsp; ';
      }
      if (name.length > 50)
        str += '<b style="font-size:.6em">' + name + "</b></h3>";
      else if (name.length > 40)
        str += '<b style="font-size:.7em">' + name + "</b></h3>";
      else if (name.length > 30)
        str += '<b style="font-size:.8em">' + name + "</b></h3>";
      else if (name.length > 20)
        str += '<b style="font-size:.9em">' + name + "</b></h3>";
      else
        str += name + "</h3>";
    } else if (itemA == "Interfaces" && typ == "host") {
       str += parseTransports(obj, itemA, itemB)
    } else if (itemA == "Transports") {
       str += parseTransports(obj, itemA, itemB)
    } else if (itemA == "Interfaces") {
       str += parseInterfaces(obj, itemA, itemB)
    } else if (itemA == "Subsystems") {
       str += parseSubsystems(obj, itemA, itemB)
    } else if (itemA == "PortIDs") {
       str += parsePortIDs(obj, itemA, itemB)
    } else if (itemA == "NSDevices") {
       str += parseNSDevs(obj, itemA, itemB)
    } else if (itemA == "Shared" || itemA == "Restricted") {
       str += parseACL(obj, itemA, itemB)
    } else {
      listC.forEach(function(itemC) {
        var listD = obj[itemA][itemB][itemC];
        str += '<p class="data">' + "--++--" + itemC + "=" + listD + "</p>";
      });
    }
  });

  if (closeDiv)
    str += "</div>";

  return str;
}

function parseName(obj, itemA) {
  var str = "";
  str += "<h1>" + itemA + ": " + obj[itemA] + " &nbsp;";
  str += '<img src="pencil.png" alt="get" class="icon" onclick="loadEdit()">';
  str += ' &nbsp;';
  str += '<img src="back.png" alt="get" class="icon" onclick="showList()">';
  str += "</h1>";
  return str;
}

function parseString(obj, itemA) {
  var str = "";
  var args = "";

  str += '<p class="data">' + itemA + ": " + obj[itemA] + "</p>";

  args = obj[itemA];
  $("#args").html(args);

  return str;
}

function parseNumber(obj, itemA) {
  var str = "";
  var args = "";
  var val = obj[itemA];

  if (val == undefined) val = 0;

  str += '<p class="data">' + itemA + ": ";
  if (val)
    str += val + " minute" + ((val == 1) ? "" : "s");
  else
    str += "disabled";

  str += "</p>";

  args += val;
  $("#parentargs").html(args);

  return str;
}

function modifySort(a) {
  if (a == "Alias" || a == "Name" || a == "Refresh")
    a = "A" + a;
  else if (a == "MgmtMode")
    a = "B" + a;
  else if (a == "Interface")
    a = "C" + a;
  else
    a = "D" + a;
  return a;
}
function customSort(a,b) {
  a = modifySort(a);
  b = modifySort(b);
  if (a > b) return 1;
  if (a < b) return -1;
  return 0;
}
function basicSort(list) {
  return function(a,b) {
    if (list[a] > list[b]) return 1;
    if (list[a] < list[b]) return -1;
    return 0;
  }
}

function parseJSON(json, object) {
  var str = "";
  var obj = jQuery.parseJSON(json);
  var listA = Object.keys(obj).sort(customSort);

  listA.forEach(function(itemA) {
    if (itemA == "Alias" || itemA == "Name")
      str += parseName(obj, itemA);
    else if (itemA == "Refresh")
      str += parseNumber(obj, itemA);
    else if (itemA == "HOSTNQN")
      str += parseString(obj, itemA);
    else if (itemA == "MgmtMode")
      str += parseMgmtMode(obj, itemA);
    else if (itemA == "Interface")
      str += parseInterface(obj, itemA);
    else
      str += parseObject(obj, itemA);
  });
  object.innerHTML = str;
}

function showError(message, str) {
  message.innerHTML =
    '<p class="error">Failed to connect.</p>' +
    "<p>" + str + "</p>" +
    "<p><b>Please try again.</b></p>";
  clearSession();
}

function loadDoc(page) {
  var xhttp = new XMLHttpRequest();
  var auth = sessionStorage.dem_auth;
  var url = "http://";
  var typ = $("#objectType").html();
  var obj = $("#objectValue").html();
  var filter = $("#filter").html();

  xhttp.onreadystatechange = function() {
    var cur_page;

    if (this.readyState != 4)
      return;

    message = document.getElementById("loginMessage");

    if (typ == "dem")
      cur_page = document.getElementById("contentPage");
    else if (obj == "")
      cur_page = document.getElementById("listPage");
    else
      cur_page = document.getElementById("detailPage");

    if (this.status == 200) {
      if ($("#addrForm").is(":visible")) {
        $("#addrForm").hide();
        $("#contentPage").show();
        $("#menu").show();
      } else if (cur_page == document.getElementById("contentPage")) {
        $("#contentPage").show();
        $("#menu").show();
      }

      if (this.responseText[0] == '{')
        parseJSON(this.responseText, cur_page);
      else
        cur_page.innerHTML = this.responseText;

      $("#form input[name=pswd]").val("");
      message.innerHTML = "<p><b>Please Login.</b></p>";
    } else if (this.status == 0)
      showError(message, "DEM is not responding.");
    else if (this.status == 403)
      showError(message, "Invalid user id and/or password.");
    else
      cur_page.innerHTML =
        "Error " + this.status + " : " + this.responseText;
  };
  xhttp.onerror = function () {
    console.log(xhttp.responseText);
  };

  if (sessionStorage.dem_addr == "" || sessionStorage.dem_port == "") {
    showError(message, "Invalid user id and/or password.");
    return;
  }

  url += sessionStorage.dem_addr + ":" + sessionStorage.dem_port + "/";

  $("#parentUri").html(page);
  $("#renamedUri").html("");

  if (typ == "dem")
      page = url + page;
  else {
    if (obj == "")
      page = url + typ + filter;
    else
      page = url + typ + "/" + page;
  }

  xhttp.open("GET", page, true);
  xhttp.setRequestHeader("Content-type", "text/plain; charset=UTF-8");
  xhttp.setRequestHeader("Access-Control-Allow-Origin", "*");
  xhttp.setRequestHeader('Authorization', auth);
  xhttp.send();
}

function checkAddress() {
    if (typeof(Storage) != "undefined") {
        if (sessionStorage.dem_addr == null ||
            sessionStorage.dem_addr == "undefined") {
            $("#addrForm").show();
            $("#first").focus();
        } else {
            $("#loginMessage").html("<p>Connecting</p>");
            showContents("dem");
        }
    } else
        $("#notSupported").show();
}

function email(name) {
    var email = "";
    var cc = "";
    var subject = "";
    var body = "";
    var cayton = "Phil Cayton <phil.cayton@intel.com>";
    var jay = "Jay Sternberg <jay.e.sternberg@intel.com>";

    if (name == "cayton") {
        email = cayton;
        cc = jay;
    } else {
        email = jay;
        cc = cayton;
    }
    subject = "NVMe-oF Discovery Manager Query";
    body = "Can you please provide more information regarding your " +
           "implementation of the DEM.  Thanks.";

    window.location.href = "mailto:" + encodeURI(email) +
                           "?cc=" + encodeURI(cc) +
                           "&subject=" + encodeURI(subject) +
                           "&body=" + encodeURI(body);
}

function openDialog(str, verb, uri) {
  $("#editVerb").html(verb);
  $("#editUri").html(uri);

  str += "<p id='err'></p>";

  document.getElementById("editPage").style.width = "100%";
  $("#editForm").keyup(function (e) {
    if (e.key === "Enter")
      closeDialog(true);
    if (e.key === "Escape")
      closeDialog(false);
  });
  window.setTimeout(function() {
    $("#editForm").html(str);
    if ($("#alias").length) 
      $("#alias").focus().select();
    else if ($("#nsid").length)
      $("#nsid").focus().select();
    else if ($("#portid").length)
      $("#portid").focus().select();
    else if ($("#subnqn").length)
      $("#subnqn").focus().select();
    else if ($("#group").length)
      $("#group").focus().select();
    }, 400);
}

function validateForm() {
  var field;
  var badfield;
  var fam;
  var name;
  var str = "";

  if ($("#alias").length) {
    field = $("#alias");
    name = field.val();
    if (!validateAlias(name)) {
      str += "<p>Invalid Alias: must be 1 - 64 characters, ";
      str += " valid characters are alphanumerics";
      str += " plus the following special characters # < > _ : . -";
      if (badfield == undefined) badfield = field;
    }
    fam = $("#objectType").html() + "/" + $("#objectValue").html();
    if (name != $("#parentUri").html() && fam == $("#editUri").html())
      $("#renamedUri").html(name);
    fam = undefined;
  }
  if ($("#group").length) {
    field = $("#group");
    name = field.val();
    if (!validateAlias(name)) {
      str += "<p>Invalid Group name: must be 1 - 64 characters, ";
      str += " valid characters are alphanumerics";
      str += " plus the following special characters # < > _ : . -";
      if (badfield == undefined) badfield = field;
    }
    if (name != $("#parentUri").html())
      $("#renamedUri").html(name);
  }
  if ($("#adr").length) {
    field = $("#adr");
    fam = $("#fam").val();
    if (fam == "ipv4") {
      if (!validateIPv4(field.val())) {
        str += "<p>Invalid IPv4 address: must be format is x.x.x.x ";
        str += " where values are decimal numbers from 0 to 255";
        if (badfield == undefined) badfield = field;
      }
    } else if (fam == "ipv6") {
      if (!validateIPv6(field.val())) {
        str += "<p>Invalid IPv6 address: must be format is x:x:x:x:x:x ";
        str += " where values may be omitted or hex numbers from 0 to FFFF";
        str += " (either upper of lower case hex is valid)";
        if (badfield == undefined) badfield = field;
      }
    } else if (fam == "fc") {
      if (!validateFC(field.val())) {
        str += "<p>Invalid FC address:";
        str += " must be format is xx:xx:xx:xx:xx:xx:xx:xx ";
        str += " where values may be omitted or hex numbers from 00 to FF";
        str += " (either upper of lower case hex is valid)";
        if (badfield == undefined) badfield = field;
      }
    } else if ($("#typ").length) {
      field = $("#fam");
      str += "<p>Invalid Address Family. Please select from ipv4, ipv6, or fc";
      if (badfield == undefined) badfield = field;
    }
    if ($("#typ").val() == "") {
      field = $("#typ");
      str += "<p>Invalid Fabric Type. Please select from rdma, tcp, or fc";
      if (badfield == undefined) badfield = field;
    }
    if ($("#typ").val() == "fc" && fam != "fc") {
      field = $("#fam");
      str += "<p>Invalid Address Family. Please select 'fc' for Family";
      str += " when Fabric is Fiber Channel.";
      if (badfield == undefined) badfield = field;
    }
    if ($("#typ").val() != "fc" && fam == "fc") {
      field = $("#fam");
      str += "<p>Invalid Address Family. Please select 'ipv4 or ipv6 for";
      str += " Family when Fabric is not Fiber Channel.";
      if (badfield == undefined) badfield = field;
    }
    if ($("#svc").length && $("#svc").val() == "") {
      if ($("#typ").length || $("#adr").val() != "") {
        field = $("#svc");
        str += "<p>Invalid Transport Service ID."
        str += " Please specify the transport service ID to use.";
        if (badfield == undefined) badfield = field;
      }
    }
  }
  if ($("#subnqn").length) {
    field = $("#subnqn");
    if (!validateNQN(field.val())) {
      str += "<p>Invalid Subsystem NQN: must be 6 - 128 characters,";
      str += " valid characters are alphanumerics";
      str += " plus the following special characters @ + _ : . -";
      if (badfield == undefined) badfield = field;
    }
  }
  if ($("#hostnqn").length) {
    field = $("#hostnqn");
    if (!validateNQN(field.val())) {
      str += "<p>Invalid Host NQN: must be 6 - 128 characters, ";
      str += " valid characters are alphanumerics";
      str += " plus the following special characters @ + _ : . -";
      if (badfield == undefined) badfield = field;
    }
  }
  if ($("#portid").length) {
    field = $("#portid");
    if (field.val() == "") {
      str += "<p>Port ID must be provided";
      if (badfield == undefined) badfield = field;
    }
  }
  if ($("#nsid").length) {
    field = $("#nsid");
    if (field.val() == "") {
      str += "<p>Namespace ID must be provided";
      if (badfield == undefined) badfield = field;
    }
  }
  if ($("#devid").length) {
    field = $("#devid");
    if (field.val() == "" || field.val() < -1) {
      str += "<p>Invalid Device ID, >= 0 for actual nvme device,";
      str += " -1 for nullb0";
      if (badfield == undefined) badfield = field;
    }
  }
  if ($("#devnsid").length) {
    field = $("#devnsid");
    if ((field.val() == "" && $("#devid").val() != -1) ||
      (field.val() < 0)) {
      str += "<p>Invalid Device Namespace ID, must be provided";
      str += " if Device ID is >= 0";
      if (badfield == undefined) badfield = field;
    }
  }

  if (badfield != undefined) {
    $("#err").html(str);
    badfield.focus();
    badfield.select();
    return false;
  }

  return true;
}

function closeDialog(execute) {
    $("#err").html("");

    if (execute) {
      if (!validateForm())
        return;

      if (!sendRequest())
        return;
    }

    document.getElementById("editPage").style.width = "0%";
    $("#editVerb").html("");
    $("#editUri").html("");
    window.setTimeout(function() { $("#editForm").html(""); }, 80);
}

function buildJSON(url) {
  var str = '{';

  if ($("#hostnqn").length)
    str += '"Alias":"' + $("#alias").val() + '",' +
           '"HOSTNQN":"' + $("#hostnqn").val() + '"';
  else if ($("#group").length)
    str += '"Name":"' + $("#group").val() + '"';
  else if ($("#refresh").length) {
    str += '"Alias":"' + $("#alias").val() + '"';
    if ($("#refresh").val() != "")
      str += ',"Refresh":' + $("#refresh").val();
    if ($("#mode").val() == "oob") {
      str += ',"MgmtMode":"OutOfBandMgmt"';
      str += ',"Interface":{';
      if ($("#fam").val() != "")
        str += '"FAMILY":"' + $("#fam").val() + '",' +
               '"ADDRESS":"' + $("#adr").val() + '",' +
               '"PORT":' + $("#svc").val();
      str += '}';
    } else if ($("#mode").val() == "ib")
      str += ',"MgmtMode":"InBandMgmt"';
    else
      str += ',"MgmtMode":"LocalMgmt"';
  } else if ($("#allowany").length) {
    str += '"SUBNQN":"' + $("#subnqn").val() + '","AllowAnyHost":';
    if ($("#allowany").is(':checked'))
      str += "1";
    else
      str += "0";
  } else if ($("#devid").length)
    str += '"NSID":' + $("#nsid").val() + ',' +
           '"DeviceID":' + $("#devid").val() + ',' +
           '"DeviceNSID":' + $("#devnsid").val();
  else if ($("#portid").length)
    str += '"PORTID":' + $("#portid").val() + ',' +
           '"TRTYPE":"' + $("#typ").val() + '",' +
           '"ADRFAM":"' + $("#fam").val() + '",' +
           '"TRADDR":"' + $("#adr").val() + '",' +
           '"TRSVCID":' + $("#svc").val();
  else if ($("#typ").length)
    str += '"TRTYPE":"' + $("#typ").val() + '",' +
           '"ADRFAM":"' + $("#fam").val() + '",' +
           '"TRADDR":"' + $("#adr").val() + '",' +
           '"TRSVCID":' + $("#svc").val();
  else if ($("#alias").length)
    str += '"Alias":"' + $("#alias").val() + '"';

  str += "}";

  return str;
}

function sendRequest() {
  var xhttp = new XMLHttpRequest();
  var auth = sessionStorage.dem_auth;
  var url = "http://";
  var verb = $("#editVerb").html();
  var page = $("#editUri").html();

  $("#post_err").html("");

  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status != 200)
         $("#post_err").html("Error " + this.status + " : " + this.responseText);
  };
  xhttp.onerror = function () {
    console.log(xhttp.responseText);
  };

  url += sessionStorage.dem_addr + ":" + sessionStorage.dem_port + "/";

  xhttp.open(verb, url + page, false);
  xhttp.setRequestHeader("Content-type", "text/plain; charset=UTF-8");
  xhttp.setRequestHeader("Access-Control-Allow-Origin", "*");
  xhttp.setRequestHeader('Authorization', auth);
  if (verb == "DELETE")
    xhttp.send();
  else
    xhttp.send(buildJSON(page));

  if ($("#post_err").html() == "") {
    if ($("#objectValue").html() == "" || $("#renamedUri").html() == "")
      page = $("#parentUri").html();
    else {
      page = $("#renamedUri").html();
      $("#objectValue").html(page);
    }

    loadDoc(page);
    return true;
  }

  if ($("err") != undefined)
    $("#err").html($("#post_err").html());
  else
    alert($("#post_err").html());

  return false;
}

function saveVal(str) {
    $("#args").html(str);
}

function validateIPv4(addr) {
  var pattern = /^(([0-9]{1,2}|[0-1][0-9]{2}|2[0-4][0-9]|25[0-5])[.]){3}([0-9]{1,2}|[0-1][0-9]{2}|2[0-4][0-9]|25[0-5])$/;
  return pattern.test(addr);
}

function validateIPv6(addr) {
  var pattern = /^([0-9a-fA-F]{0,4}[:]){5}[0-9a-fA-F]{0,4}$/;
  return pattern.test(addr);
}

function validateFC(addr) {
  var pattern = /^([0-9a-fA-F]{2}[:]){7}[0-9a-fA-F]{2}$/;
  return pattern.test(addr);
}

function validateAlias(str) {
  var pattern = /^[0-9a-zA-Z#<>_:.-]{1,64}$/;
  return pattern.test(str);
}

function validateNQN(str) {
  var pattern = /^[0-9a-zA-Z@+_:.-]{6,128}$/;
  return pattern.test(str);
}
