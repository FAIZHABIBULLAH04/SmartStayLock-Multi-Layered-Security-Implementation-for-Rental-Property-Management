<?php

include 'db.php';

$name = $_GET['name'];
$phone = $_GET['phone'];
$checkin = $_GET['checkin'];
$checkout = $_GET['checkout'];

$sql = "INSERT INTO tenants
(
tenant_name,
phone,
checkin_date,
checkout_date,
status
)
VALUES
(
'$name',
'$phone',
'$checkin',
'$checkout',
'ACTIVE'
)";

if(mysqli_query($conn,$sql))
{
    $tenant_id = mysqli_insert_id($conn);

    mysqli_query($conn,"
    INSERT INTO access_logs
    (
    action,
    status,
    user_name,
    role
    )
    VALUES
    (
    'tenant_registered',
    'success',
    '$name',
    'Tenant'
    )");

    echo "Tenant Registered";
}
else
{
    echo "Failed";
}

?>