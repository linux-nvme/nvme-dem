<?php

session_start();

$_POST = json_decode(file_get_contents('php://input'), true);
if(isset($_POST) && !empty($_POST)) {
    
    $username = $_POST['username'];
    $password = $_POST['password'];

    if($username == 'admin' && $password == 'admin') {
        $_SESSION['user'] = 'admin';
        ?>
        {
            "success": true,
            "message" : "admin"
        }

        <?php
    } else if ($username == 'customer' && $password == 'customer') {
        $_SESSION['user'] = 'customer';
        ?>
        {
            "success": true,
            "message" : "customer"
        }
        <?php
    } else {
        ?>
        {
            "success": false,
            "message": "Invalid Credentials"
        }

        <?php
    }
} else {
    ?>
    {
        "success":  false,
        "message":  "Only Post access accepted"
    }
    <?php
}
?>