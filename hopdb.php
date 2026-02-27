<?php
//Code by ThanhPhan
//tele: t.me/naruto999x

ob_start();
ini_set("display_errors", "on");
ini_set('error_reporting', E_ALL);
ini_set('safe_mode', '1');
date_default_timezone_set('Asia/Ho_Chi_Minh');
set_time_limit(86400);
//-----------------------//
//--------fix name change server + remove char for level
//-----------------------//
$dbhost = '127.0.0.1:3306';
$dbuser = 'thanhp';
$dbpass = '!@#$%^&*(99999';


$db1 = 'db1'; // data sv1 tuyetk_s1
$db2 = 'db2'; // data sv1
$dbhop = 'dbhop'; // data này phải là data trắng để hợp 2 db vào làm 1

$refixtable1 = '_s1'; // nên để nguyên 
$refixtable2 = '_s2'; // nên để nguyên

$log_content = '';

$conn = new mysqli($dbhost, $dbuser, $dbpass);

if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

if ($_SERVER['REQUEST_METHOD'] == 'POST' && $_SERVER['HTTP_REFERER']) {
    //-----------------------//
    //--------
    //-----------------------//
    if (isset($_POST['fix1'])) {
        $level = $_POST['level'];
        if ($level == "") {
            $log_content .= "Vui lòng nhập vào cấp độ cần xóa!!!";
        } else {
            $conn->select_db($dbhop);

            $arrTable = array(
                't_char',
                't_ability',
                't_charextra',
                //'t_iteminfo',
                't_pet',
                't_skill',
                't_xinfa'
            );
            $result = $conn->query("SELECT charguid FROM t_char WHERE level <= $level");

            if (!$result) {
                die("Query failed: " . $conn->error);
            }

            while ($row = $result->fetch_assoc()) {
                $charguid = $row['charguid'];
                foreach ($arrTable as $table) {
                    $table_fix = $table;// . '_copy';
                    $deleteResult = $conn->query("DELETE FROM $table_fix WHERE charguid='$charguid'");

                    if ($deleteResult) {
                        $log_content = "Xóa nhân vật dbhop thành công!!";
                    } else {
                        $log_content = "Xóa nhân vật dbhop thất bại!!";
                    }
                }
            }
            $result->free();
        }
    } else if (isset($_POST['fix2'])) {
        $conn->select_db($dbhop);

        $charnamefix = $_POST['namefix'];
        if ($charnamefix == "") {
            $log_content .= "Nhập vào char name fix!!";
        } else {
            $t_char_fix = "t_char" . $refixtable2;
            $result = $conn->query("SELECT charname, charguid FROM $t_char_fix");

            if (!$result) {
                die("Query failed: " . $conn->error);
            }

            while ($row = $result->fetch_assoc()) {
                set_time_limit(86400);
                $charguid = $row['charguid'];
                $charname = $row['charname'];
                $charname_fix = $charnamefix . '.' . $charname . '*';

                $query = $conn->query("UPDATE $t_char_fix SET charname='$charname_fix' WHERE charguid=$charguid AND charname='$charname'");

                if (!$query) {
                    $log_content .= "Fix charname $t_char_fix thất bại!!";
                }else{
				     $log_content .= "Fix charname $t_char_fix thành công!!";
				}
            }
            $result->free();
        }
    } elseif (isset($_POST['fix3'])) {
    $conn->select_db($dbhop);

    $arrTable = array(
        't_char',
        't_ability',
        't_charextra',
        't_iteminfo',
        't_pet',
        't_skill',
        't_xinfa'
    );

    // Lấy charguid max từ t_char1
    $result1 = $conn->query("SELECT MAX(charguid) AS max_charguid FROM t_char1");
    $row1 = $result1->fetch_assoc();
    $charguidmaxdb1 = $row1['max_charguid'];
    $log_content = "Charguid max db1: $charguidmaxdb1";

    if ($charguidmaxdb1 === null) {
        $log_content .= " - Không lấy được max charguid db1!";
    } else {
        // Lấy danh sách charguid từ t_char2
        $result2 = $conn->query("SELECT charguid FROM t_char2");
        
        if ($result2) {
            while ($row2 = $result2->fetch_assoc()) {
                $charguid = $row2['charguid'];
                // Tăng giá trị charguidfix lên để tránh trùng lặp
                $charguidfix = $charguidmaxdb1 + 1;
                $charguidmaxdb1++; // Tăng giá trị charguidmaxdb1 để không lặp lại

                foreach ($arrTable as $table) {
                    $table_fix = $table . $refixtable2;
                    // Sử dụng prepared statement để cập nhật charguid và hpetguid (nếu có)
                    if ($table == "t_pet") {
                        $stmt = $conn->prepare("UPDATE $table_fix SET charguid=?, hpetguid=? WHERE charguid=?");
                        $stmt->bind_param("iii", $charguidfix, $charguidfix, $charguid);
                        $query = $stmt->execute();
                    } else {
                        $stmt = $conn->prepare("UPDATE $table_fix SET charguid=? WHERE charguid=?");
                        $stmt->bind_param("ii", $charguidfix, $charguid);
                        $query = $stmt->execute();
                    }

                    if (!$query) {
                        $log_content .= " - Fix charguid $table_fix $charguid thất bại!<br>";
                    } else {
                        $log_content .= " - Fix charguid $table_fix $charguid --> $charguidfix thành công!<br>";
                    }
                }
            }
        } else {
            $log_content .= " - Lỗi khi lấy danh sách charguid từ t_char2!";
        }
    }

    // Đóng kết nối và giải phóng bộ nhớ
    $result1->free();
    $result2->free();
}

 else if (isset($_POST['fix4'])) {
        $conn->select_db($dbhop);
        $arrTable = array(
            't_char',
            't_ability',
            't_charextra',
            't_iteminfo',
            't_pet',
            't_skill',
            't_xinfa'
        );
        foreach ($arrTable as $table) {
            set_time_limit(86400);
            $data1 = $table . $refixtable1;
            $data2 = $table . $refixtable2;

            $result = $conn->query("INSERT INTO $table (SELECT * FROM $data1 UNION ALL SELECT * FROM $data2)");

            if ($result) {
                $log_content .= "Hợp table $table thành công!!</br>";
            } else {
                $log_content .= "Hợp table $table thất bại!!";
            }
        }
    } else if (isset($_POST['fix11'])) {
    $conn->select_db($dbhop);
    $idItem = $_POST['idItem'];
    if ($idItem == "") {
        $log_content .= "Nhập vào ID Item!!</br>";
    } else {
        $result = $conn->query("DELETE FROM t_iteminfo WHERE itemtype=$idItem");
        if ($result) {
            $log_content .= "Xóa item $idItem thành công!!</br>";
        } else {
            $log_content .= "Xóa item $idItem thất bại!!</br>";
        }
    }
} elseif (isset($_POST['fix12'])) {
    $conn->select_db($dbhop);

    $result = $conn->query("SELECT charguid, relflag, shopinfo from t_char") or die($conn->error);
    while ($key = $result->fetch_assoc()) {
        set_time_limit(86400);
        $charguid = $key['charguid'];
        $relflag = $key['relflag'];
        $relflag1 = substr($relflag, 0, 76);
        $relflag2 = substr($relflag, 84, strlen($relflag));
        $relflag_new = $relflag1 . 'FFFFFFFF' . $relflag2;

        $shopinfo = 'FFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFFFFFF0000FFFFFFFF';
        $result1 = $conn->query("UPDATE t_char SET relflag='$relflag_new' WHERE charguid=$charguid") or die($conn->error);
        $result2 = $conn->query("UPDATE t_char SET shopinfo='$shopinfo' WHERE charguid=$charguid") or die($conn->error);
        if (!$result2) {
            $log_content = "Hủy shop thương hội dbhop thất bại!!";
        }else{
		    $log_content = "Hủy shop thương hội dbhop thành công!!";
		}
        if (!$result1) {
            $log_content = "Hủy kết hôn dbhop thất bại!!";
        }else{
		    $log_content = "Hủy kết hôn dbhop thành công!!";
		}
    }
    $result->free();
} else if (isset($_POST['fix15'])) {
    $conn->select_db($db1);
    $conn->select_db($db2);
    $conn->select_db($dbhop);

    $arrTable = array(
        't_char',
        't_ability',
        't_charextra',
        't_iteminfo',
        't_pet',
        't_skill',
        't_xinfa'
    );

    foreach ($arrTable as $table) {
        set_time_limit(86400);
        $data1 = $table . $refixtable1;
        $data2 = $table . $refixtable2;

        // Sử dụng một giao dịch duy nhất để có hiệu suất và xử lý lỗi tốt hơn
        $conn->begin_transaction();

        try {
            // Create the first copy table in dbhop
            $query1 = "CREATE TABLE IF NOT EXISTS $dbhop.$data1 LIKE $db1.$table";
            $conn->query($query1);

            // Insert data from db1 to data1 in dbhop
            $query2 = "INSERT INTO $dbhop.$data1 SELECT * FROM $db1.$table";
            $conn->query($query2);

            // Create the second copy table in dbhop
            $query3 = "CREATE TABLE IF NOT EXISTS $dbhop.$data2 LIKE $db2.$table";
            $conn->query($query3);

            // Insert data from db2 to data2 in dbhop
            $query4 = "INSERT INTO $dbhop.$data2 SELECT * FROM $db2.$table";
            $conn->query($query4);

            // Commit transaction if all queries succeed
            $conn->commit();

            $log_content .= "Copy dữ liệu $db1-$data1 -- $db2-$data2 sang dbhop thành công!!</br>";
        } catch (Exception $e) {
            // Rollback the transaction on failure
            $conn->rollback();

            $log_content .= "Failed to copy data for table $table: " . $e->getMessage() . "</br>";
        }
    }
} else if (isset($_POST['fix16'])) {
    $conn->select_db($dbhop);

    $arrTable = array(
        't_char',
        't_ability',
        't_iteminfo',
        't_pet',
        't_skill',
        't_xinfa'
    );
    foreach ($arrTable as $table) {
        set_time_limit(86400);
        $data1 = $table . $refixtable1;
        $data2 = $table . $refixtable2;

     //   $result = $conn->query("ALTER TABLE $table DROP COLUMN aid") or die($conn->error);
        $result = $conn->query("ALTER TABLE $table DROP COLUMN aid");
        $result1 = $conn->query("ALTER TABLE $data1 DROP COLUMN aid");
        $result2 = $conn->query("ALTER TABLE $data2 DROP COLUMN aid");

        if (!$result) {
            $log_content .= "Failed $table !!</br>";
        }
        if (!$result1) {
            $log_content .= "Failed $data1 !!</br>";
        }
        if (!$result2) {
            $log_content .= "Failed $data2 !!</br>";
        }
        $log_content .= "Xóa aid $table -- $data1 -- $data2 thành công!!</br>";
    }
} else if (isset($_POST['fix17'])) {
    $conn->select_db($dbhop);

    $arrTable = array(
        'ALTER TABLE t_char ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_ability ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_iteminfo ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_pet ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_skill ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_xinfa ADD aid bigint(20) NOT NULL auto_increment PRIMARY KEY',
        'ALTER TABLE t_char ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_char ADD UNIQUE KEY Index_char_charguid (charguid)',
        'ALTER TABLE t_char ADD UNIQUE KEY Index_char_charname (charname)',
        'ALTER TABLE t_char ADD KEY Index_char_accname (accname)',
        'ALTER TABLE t_char ADD KEY Index_char_level (level, exp)',
        'ALTER TABLE t_char ADD KEY Index_char_yuanbao (yuanbao)',
        'ALTER TABLE t_ability ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_ability ADD KEY Index_ab_charguid (charguid)',
        'ALTER TABLE t_iteminfo ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_iteminfo ADD UNIQUE KEY Index_it_charguid (charguid, pos)',
        'ALTER TABLE t_iteminfo ADD KEY Index_it_itemguid (guid, world, server)',
        'ALTER TABLE t_iteminfo ADD KEY Index_iteminfo_itemtype (itemtype, isvalid)',
        'ALTER TABLE t_pet ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_pet ADD KEY Index_Pet_Charguid (charguid)',
        'ALTER TABLE t_skill ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_skill ADD KEY Index_sk_charguid (charguid)',
        'ALTER TABLE t_xinfa ADD PRIMARY KEY (aid)',
        'ALTER TABLE t_xinfa ADD KEY Index_xinfa_charguid (charguid)'
    );

    foreach ($arrTable as $query) {
        set_time_limit(86400);
        $result = $conn->query($query);// or die($conn->error);
        if (!$result) {
            $log_content .= "Failed !!</br>";
        }else{
        $log_content .= "Add aid $query thành công!!</br>";
		}
    }
} else if (isset($_POST['fix18'])) {
    $conn->select_db($dbhop);

    $arrTable = array(
        't_char',
        't_ability',
        't_charextra',
        't_iteminfo',
        't_pet',
        't_skill',
        't_xinfa'
    );
    foreach ($arrTable as $table) {
        $data1 = $table . $refixtable1;
        $data2 = $table . $refixtable2;

        $result = $conn->query("DROP TABLE $data1") or die($conn->error);
        $result1 = $conn->query("DROP TABLE $data2") or die($conn->error);

        if (!$result) {
            $log_content .= "Failed 1!!</br>";
        }
        if (!$result1) {
            $log_content .= "Failed 2!!</br>";
        }

        $log_content .= "Xóa table $data1 -- $data2 thành công!!</br>";
    }
} else if (isset($_POST['fix19'])) {
    $conn->select_db($dbhop);

    // Tính toán và cập nhật maxcharguid trong t_var
    $result = $conn->query("SELECT MAX(charguid) AS max_charguid FROM t_char") or die($conn->error);
    $row = $result->fetch_assoc();
    $charguid = $row['max_charguid'];
    $maxcharguid = $charguid + 10;
    $result_update_var = $conn->query("UPDATE t_var SET maxcharguid=$maxcharguid") or die($conn->error);

    if ($result_update_var) {
        $log_content .= "Update t_var $maxcharguid thành công!!</br>";
    } else {
        $log_content .= "Update t_var thất bại!!</br>";
    }

    // Tính toán và cập nhật serial trong t_itemkey
    $result1 = $conn->query("SELECT MAX(guid) AS max_guid FROM t_iteminfo") or die($conn->error);
    $row1 = $result1->fetch_assoc();
    $guidmaxdbhop = $row1['max_guid'];
   // $log_content .= " guid max item dbhop $guidmaxdbhop!!</br>";

    $serialfix = $guidmaxdbhop + ($guidmaxdbhop / 2);
    $querry_max = $conn->query("UPDATE t_itemkey SET serial=$serialfix WHERE aid=1 AND smkey=7001") or die($conn->error);

    if ($querry_max) {
        $log_content .= " Fix t_itemkey $serialfix thành công!!</br>";
    } else {
        $log_content .= " Fix t_itemkey thất bại!!</br>";
    }
} else if (isset($_POST['fix10'])) {
    $conn->select_db($dbhop);
    $table1 = "t_iteminfo";

    // Lấy guid lớn nhất từ t_iteminfo
    $result = $conn->query("SELECT MAX(guid) AS max_guid FROM $table1") or die($conn->error);
    $guidmaxdb1 = $result->fetch_assoc()['max_guid'];
  //  $log_content .= "guid max $guidmaxdb1</br>";

    // Lấy danh sách guid trùng lặp
    $query = "SELECT aid, guid FROM $table1 WHERE guid IN (SELECT guid FROM $table1 GROUP BY guid HAVING COUNT(guid) > 1) ORDER BY guid";
    $stmt = $conn->prepare($query);
    $stmt->execute();
    $result1 = $stmt->get_result();

    while ($row = $result1->fetch_assoc()) {
        set_time_limit(86400);
        $aid = $row['aid'];
        $guidtrunglap = $row['guid'];

        if ($guidtrunglap > 0) {
            $log_content .= "guid trùng lặp item $aid -- $guidtrunglap!!</br>";
            // Tính toán guid mới không trùng lặp
            $guidfix = $guidmaxdb1 + 1;
            $guidmaxdb1++;
            while (true) {
                // Kiểm tra xem guidfix có tồn tại không
                $check_stmt = $conn->prepare("SELECT COUNT(*) AS count FROM $table1 WHERE guid = ?");
                $check_stmt->bind_param("i", $guidfix);
                $check_stmt->execute();
                $check_result = $check_stmt->get_result();
                $check_row = $check_result->fetch_assoc();
                $count = $check_row['count'];

                if ($count == 0) {
                    break; // guidfix không trùng lặp, thoát vòng lặp
                } else {
                    $guidfix++; // Tăng giá trị guidfix lên để kiểm tra lại
                }
            }

            // Update guid mới vào t_iteminfo
            $update_stmt = $conn->prepare("UPDATE $table1 SET guid = ? WHERE guid = ? AND aid = ?");
            $update_stmt->bind_param("iii", $guidfix, $guidtrunglap, $aid);
            $query_update = $update_stmt->execute();

            if ($query_update) {
                $log_content .= "Fix $guidtrunglap $table1 thành công!!</br>";
            } else {
                $log_content .= "Fix $guidtrunglap $table1 thất bại!!</br>";
            }
        } else {
            $log_content = "Không có trùng lặp!!</br>";
        }
    }

    // Giải phóng tài nguyên
    $result1->free();
    
    $table12 = "t_pet";

    // Lấy giá trị lớn nhất của lpetguid từ t_pet
    $result2 = $conn->query("SELECT MAX(lpetguid) AS max_lpetguid FROM $table12") or die($conn->error);
    $result_array2 = $result2->fetch_assoc();
    $lpetguid = $result_array2['max_lpetguid'];
 //   $log_content .= "lpetguid max $lpetguid!!<br>";

    // Lấy danh sách lpetguid trùng lặp
    $query2 = "SELECT aid, lpetguid FROM $table12 WHERE lpetguid IN (SELECT lpetguid FROM $table12 GROUP BY lpetguid HAVING COUNT(lpetguid) > 1) ORDER BY lpetguid";
    $stmt2 = $conn->prepare($query2);
    $stmt2->execute();
    $result2 = $stmt2->get_result();

    while ($row = $result2->fetch_assoc()) {
        set_time_limit(86400);
        $aid = $row['aid'];
        $lpetguidtrunglap = $row['lpetguid'];

        if ($lpetguidtrunglap > 0) {
            $log_content .= "lpetguid trùng lặp item $aid -- $lpetguidtrunglap!!</br>";
            // Tính toán lpetguid mới không trùng lặp
            $guidfix = $lpetguid + 1;
            $lpetguid++;
            while (true) {
                // Kiểm tra xem guidfix có tồn tại không
                $check_stmt = $conn->prepare("SELECT COUNT(*) AS count FROM $table12 WHERE lpetguid = ?");
                $check_stmt->bind_param("i", $guidfix);
                $check_stmt->execute();
                $check_result = $check_stmt->get_result();
                $check_row = $check_result->fetch_assoc();
                $count = $check_row['count'];

                if ($count == 0) {
                    break; // guidfix không trùng lặp, thoát vòng lặp
                } else {
                    $guidfix++; // Tăng giá trị guidfix lên để kiểm tra lại
                }
            }

            // Update lpetguid mới vào t_pet
            $update_stmt = $conn->prepare("UPDATE $table12 SET lpetguid = ? WHERE lpetguid = ? AND aid = ?");
            $update_stmt->bind_param("iii", $guidfix, $lpetguidtrunglap, $aid);
            $query_update = $update_stmt->execute();

            if ($query_update) {
                $log_content .= "Fix $lpetguidtrunglap $table12 thành công!!</br>";
            } else {
                $log_content .= "Fix $lpetguidtrunglap $table12 thất bại!!</br>";
            }
        } else {
            $log_content = "Không có trùng lặp!!</br>";
        }
    }

    // Giải phóng tài nguyên
    $result2->free();
}
else if (isset($_POST['fix999'])) {
    $conn->select_db($dbhop);

    $arrTable = array(
        't_char',
        't_ability',
     //   't_charextra',
        't_iteminfo',
        't_pet',
        't_skill',
        't_xinfa'
    );
    
    foreach ($arrTable as $table) {
        // Lấy charguid max từ bảng hiện tại
        $result1 = $conn->query("SELECT MAX(aid) AS max_aid FROM $table");// or die($conn->error)
        if ($result1) {
            $row1 = $result1->fetch_assoc();
            if ($row1) {
                $maxaid = $row1['max_aid'] + 1;
                $log_content .= "$table ----- aid = $maxaid<br/>";
            } else {
                $log_content .= "Không thể lấy aid từ bảng $table<br/>";
            }
            $result1->free();
        } else {
            $log_content .= "Lỗi truy vấn bảng $table: " . $conn->error . "<br/>";
        }
    }
}elseif (isset($_POST['fix7'])) {
    $conn->select_db($dbhop);

    $t_iteminfo = "t_iteminfo";
    
    // Sử dụng prepared statement và MySQLi để tránh SQL injection và cải thiện hiệu suất
    $query = "SELECT aid, guid FROM $t_iteminfo WHERE guid IN (SELECT guid FROM $t_iteminfo GROUP BY guid HAVING COUNT(guid) > 1) ORDER BY guid";
    $stmt = $conn->prepare($query);
    $stmt->execute();
    $result = $stmt->get_result();

        while ($row = $result->fetch_assoc()) {
            $aid = $row['aid'];
            $guid = $row['guid'];
			if ($guid>0) {
				$log_content .= "guid trùng lặp:  $aid    ----    $guid<br>";
			} else {
				$log_content = "Không có guid trùng lặp<br>";
			}
        }

    // Giải phóng tài nguyên
    $stmt->close();
}
elseif (isset($_POST['fix8'])) {
    $conn->select_db($dbhop);

    $t_pet = "t_pet";
    
    // Sử dụng prepared statement và MySQLi để tránh SQL injection và cải thiện hiệu suất
    $query = "SELECT aid, lpetguid FROM $t_pet WHERE lpetguid IN (SELECT lpetguid FROM $t_pet GROUP BY lpetguid HAVING COUNT(lpetguid) > 1) ORDER BY lpetguid";
    $stmt = $conn->prepare($query);
    $stmt->execute();
    $result = $stmt->get_result();

    if ($result->num_rows > 0) {
        while ($row = $result->fetch_assoc()) {
            $aid = $row['aid'];
            $lpetguid = $row['lpetguid'];
            $log_content .= "lpetguid trùng lặp:  $aid    ----    $lpetguid<br>";
        }
    } else {
        $log_content = "Không có lpetguid trùng lặp<br>";
    }

    // Giải phóng tài nguyên
    $stmt->close();
}


$conn->close();

}

?>

<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Merge DATA TLBB - TP (Ver6.9)</title>
  <!-- Bootstrap CSS -->
  <link rel="stylesheet" href="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css" integrity="sha384-JcKb8q3iqJ61gNV9KGb8thSsNjpSL0n8PARn9HuZOnIxN0hoP+VmmDGMN5t9UJ0Z" crossorigin="anonymous">
    <style>

		.log-content {
			width: 100%;
			height: 200px;
			overflow: auto;
			border: 1px solid #ccc;
			padding: 5px 0 20px 0;
		}

    </style>
</head>
<body>
  <div class="container mt-5">
        <h5>TOOL này dùng để Merge Database db2 vào db1 (CODE BY TP)</h5>
        <h5 style="color:red;">Chú ý Cấu hình Data -- Bạn đang tiến hành hợp nhất: DB1: [<?=$db1;?>] ---  DB2: [<?=$db2;?>] --- vào DBHOP: [<?=$dbhop;?>]</h5>
    <form id="formID" name="formID" action="" method="post">
      <div class="form-group">
        <label for="level">Xoá nhân vật cấp độ xx trở lại (ví dụ 50 là xoá từ cấp 0 đến cấp 50):</label>
        <input type="text" class="form-control" id="level" name="level">
      </div>
      <div class="form-group">
        <label for="namefix">Charname Fix trùng để hợp nhất (ví dụ nhập vào sv1, tên nhân vật mới sẽ có dạng: sv1.charname*):</label>
        <input type="text" class="form-control" id="namefix" name="namefix">
      </div>
      <div class="form-group">
        <label for="idItem">ID Item xoá trong t_iteminfo:</label>
        <input type="text" class="form-control" id="idItem" name="idItem">
      </div>
        <br>
        <?php if (!empty($log_content)): ?>
            <div class="alert alert-info log-content" role="alert">
                <?php echo nl2br($log_content); ?>
            </div>
        <?php endif; ?>
<div class="container mt-5">
  <div class="row">
    <div class="col-md-6">
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix1">Xóa nhân vật theo cấp độ (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix15">1. Copy db1 db2 đến (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix2">2. Sửa trùng tên nhân vật (db2)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix3">3. Sửa trùng charguid (db2)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix16">4. Xóa aid db1 + db2 + (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix4">5. Hợp db1 db2 vào (dbhop)</button>
    </div>
    <div class="col-md-6">
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix19">6. Sửa t_var + item_key (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix17">7. Thêm lại aid vào (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix12">8. Hủy kết hôn, thương hội (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix10">9. Sửa guid (t_iteminfo), lpetguid (t_pet) (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix18">10. Xóa db1 db2 trong (dbhop)</button>
      <button type="submit" class="btn btn-primary btn-block mb-3" name="fix999">Xem AID của table ('t_char',        't_ability',        't_charextra',        't_iteminfo',        't_pet',        't_skill',        't_xinfa')</button>
    </div>
    <div class="col-md-6">
		<button type="submit" class="btn btn-primary btn-block mb-3" name="fix11">Xóa item theo ID (dbhop)</button>
		<button type="submit" class="btn btn-primary btn-block mb-3" name="fix7">Kiểm tra guid trùng t_iteminfo (dbhop)</button>
		<button type="submit" class="btn btn-primary btn-block mb-3" name="fix8">Kiểm tra lpetguid  trùng t_pet (dbhop)</button>
    </div>
  </div>
</div>
    </form>

  </div>

  <!-- Bootstrap JS and dependencies -->
  <script src="https://code.jquery.com/jquery-3.5.1.slim.min.js" integrity="sha384-DfXdz2htPH0lsSSs5nCTpuj/zy4C+OGpamoFVy38MVBnE+IbbVYUew+OrCXaRkfj" crossorigin="anonymous"></script>
  <script src="https://cdn.jsdelivr.net/npm/@popperjs/core@1.16.1/dist/umd/popper.min.js" integrity="sha384-vB1gDlu2A6wI7LwkK8Zl8bdGsvjy3R6STZII4U6kMIc0EpJc3rCOm1F0aC5m6N4J" crossorigin="anonymous"></script>
  <script src="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/js/bootstrap.min.js" integrity="sha384-B4gt1jrGC7Jh4AgTPSdUtOBvfO8sh+Wy2F7S1z3VHAq1mzbh5sSvLa6N7EJfXfK1" crossorigin="anonymous"></script>
</body>
</html>

