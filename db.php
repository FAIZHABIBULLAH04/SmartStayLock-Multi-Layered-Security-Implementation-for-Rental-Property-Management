<?php

$conn = new mysqli(
    "YOUR_DB_HOST",       // e.g. localhost
    "YOUR_DB_USERNAME",   // e.g. root
    "YOUR_DB_PASSWORD",   // e.g. ""
    "YOUR_DB_NAME"        // e.g. smartstaylock
);

if ($conn->connect_error) {
    die("Connection failed");
}

?>