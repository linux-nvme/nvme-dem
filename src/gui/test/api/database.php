<?php

session_start();

$user = $_SESSION['user'];

if($user == 'admin') {
    echo '
    {
        "message": "This is secret message only for administrator",
        "success": true
    }';
} else {
    echo ' {
        "message": "Unauthorized Access",
        "success": false
    }';
}

?>