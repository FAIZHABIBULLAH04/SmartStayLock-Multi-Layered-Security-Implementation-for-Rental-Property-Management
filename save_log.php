<?php

header("Content-Type: text/plain");

// ================= API KEY =================
$valid_key = "YOUR_API_KEY";   // e.g. SMARTSTAYLOCK2026

// CHECK API KEY
if (
    !isset($_GET['api_key']) ||
    $_GET['api_key'] != $valid_key
) {
    http_response_code(403);
    die("Unauthorized Access");
}

// ================= DATABASE =================
$host = "YOUR_DB_HOST";       // e.g. localhost
$user = "YOUR_DB_USERNAME";   // e.g. root
$pass = "YOUR_DB_PASSWORD";   // e.g. ""
$db   = "YOUR_DB_NAME";       // e.g. smartstaylock

// CONNECT
$conn = new mysqli($host, $user, $pass, $db);

if ($conn->connect_error) {
    http_response_code(500);
    die("Database Connection Failed");
}

// CHECK REQUIRED PARAMETERS
if (
    !isset($_GET['action']) ||
    !isset($_GET['status'])
) {
    http_response_code(400);
    die("Missing Parameters");
}

// SANITIZE
$action = trim($_GET['action']);
$status = trim($_GET['status']);

$user_name = isset($_GET['user_name'])
    ? trim($_GET['user_name'])
    : "";

$role = isset($_GET['role'])
    ? trim($_GET['role'])
    : "";

// LIMIT LENGTH
if (
    strlen($action) > 100 ||
    strlen($status) > 100 ||
    strlen($user_name) > 100 ||
    strlen($role) > 50
) {
    http_response_code(400);
    die("Input Too Long");
}

// PREPARED STATEMENT
$stmt = $conn->prepare(
    "INSERT INTO access_logs
    (
        action,
        status,
        user_name,
        role
    )
    VALUES
    (
        ?, ?, ?, ?
    )"
);

if (!$stmt) {
    http_response_code(500);
    die("Prepare Failed");
}

// BIND
$stmt->bind_param(
    "ssss",
    $action,
    $status,
    $user_name,
    $role
);

// EXECUTE
if ($stmt->execute()) {
    echo "Log Saved";
} else {
    http_response_code(500);
    echo "Insert Failed";
}

// CLOSE
$stmt->close();
$conn->close();

?>