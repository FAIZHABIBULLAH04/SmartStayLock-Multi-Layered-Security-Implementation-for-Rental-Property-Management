<?php

include 'db.php';

$fingerprint_id = $_GET['fingerprint_id'];

$sql = "SELECT * FROM admins
        WHERE fingerprint_id='$fingerprint_id'
        LIMIT 1";

$result = mysqli_query($conn,$sql);

if(mysqli_num_rows($result)>0)
{
    echo json_encode(mysqli_fetch_assoc($result));
}
else
{
    echo "NOT_FOUND";
}

?>