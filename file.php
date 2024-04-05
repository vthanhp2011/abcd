<?php
ini_set('memory_limit', '2048M'); // Tăng giới hạn bộ nhớ lên 1GB
// Đặt giới hạn kích thước cho yêu cầu POST
//ini_set('post_max_size', '512M'); // Đặt giới hạn là 64 MB
//ini_set('upload_max_filesize', '512M'); // Đặt giới hạn kích thước tập tin tải lên cũng là 64 MB
error_reporting(1);
ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);

set_time_limit(1*(60*60)); // Tăng thời gian tối đa . (60) giây

function read_file_content($file_path) {
    $content = $file_content = '';

    // Kiểm tra xem tệp có tồn tại không
    if (!file_exists($file_path)) {
        // Nếu không tìm thấy tập tin, in ra thông báo lỗi
		echo "Không tìm thấy tập tin: $file_path\n";
    } else {
        // Nếu tập tin tồn tại, tiến hành đọc nội dung
        $file_content = file_get_contents($file_path);
        
        // Kiểm tra xem có thể đọc được nội dung hay không
		if ($file_content === false) {
            // Nếu không thể đọc nội dung, in ra thông báo lỗi
			echo "Không thể đọc nội dung tập tin: $file_path\n";
		}
    }

    // Trả về nội dung tệp đã đọc
    return $file_content;
}

// Hàm để trích xuất các khối từ nội dung và ghi chú
function extract_blocks_and_log($content, $log_file_path, $block_type, &$log_content) {
    // Khởi tạo biến để lưu pattern của khối
    $block_pattern = '';

    // Xây dựng pattern dựa vào loại khối
    if ($block_type == 'particle') {
        // Nếu loại khối là 'particle', sử dụng pattern cho khối particle
        $block_pattern = '/^\s*([^\s\{]+)\s*\{/m';
    } elseif (in_array($block_type, ['skill', 'material', 'effect'])) {
        // Nếu loại khối là 'skill', 'material', hoặc 'effect', sử dụng pattern tương ứng
        $block_pattern = '/' . $block_type . '\s+([^\s]+)(\s*:\s*[^\s]+)?\s*\{/';
    } elseif ($block_type == 'obj') {
        // Nếu loại khối là 'obj', sử dụng pattern cho khối obj
        $block_pattern = '/<Object\s+name="([^"]+)"/';
    }

    // Tìm các khối trong nội dung và lưu vào mảng $matches
    preg_match_all($block_pattern, $content, $matches, PREG_SET_ORDER);

    // Khởi tạo mảng để lưu trữ các khối duy nhất
    $blocks = array();
    foreach ($matches as $match) {
        // Thêm tên khối vào mảng
        $blocks[] = $match[1];
    }
    $blocks = array_unique($blocks);

    // Khởi tạo nội dung cho log
    $log_content = "-----------------" . $block_type . "-----------------\n";
    // Ghi tên các khối vào file log
    foreach ($blocks as $block) {
        $log_content .= $block . "\n";
    }

    // Ghi nội dung log vào file
    file_put_contents($log_file_path, $log_content);
}



// Hàm để hợp nhất các tệp và ghi chú
function merge_files($file1_path, $file2_path, $log_file_path, $block_type, &$log_content) {
    // Đọc nội dung của File 1 và File 2
    $file1_content = read_file_content($file1_path);
    $file2_content = read_file_content($file2_path);

	//var_dump($file1_content);
	//var_dump($file2_content);
    // Kiểm tra xem nội dung của cả hai File có tồn tại không
    if (empty($file1_content) || empty($file2_content)) {
        $log_content .= "Một hoặc cả hai tệp đều trống hoặc không thể truy cập được.\n";
        return;
    }

    // Kiểm tra xem loại khối được chọn có hợp lệ không
    $valid_block_types = array('particle', 'skill', 'material', 'effect', 'obj', 'StrDictionary');
    if (!in_array($block_type, $valid_block_types)) {
        // Thông báo lỗi cho người dùng
        $log_content .= "Loại khối không hợp lệ.\n";
        return;
    }

    // Tạo tên file và đường dẫn cho file log ghi lại các khối trùng lặp
    $log_file1 = 'log_file1_' . $block_type . '.txt';
    $log_file2 = 'log_file2_' . $block_type . '.txt';

    // Trích xuất các khối và ghi vào các file log
    extract_blocks_and_log($file1_content, $log_file1, $block_type, $log_content);
    extract_blocks_and_log($file2_content, $log_file2, $block_type, $log_content);

    // Đọc tên khối từ file1 từ file log
    $file1_blocks = file($log_file1, FILE_IGNORE_NEW_LINES);
    // Đọc tên khối từ file2 từ file log
    $file2_blocks = file($log_file2, FILE_IGNORE_NEW_LINES);

    // Khởi tạo mảng để lưu trữ thông tin về các khối đã được xử lý
    $processed_blocks = array();
    // Khởi tạo nội dung hợp nhất với nội dung của file1
    $merged_content = $file1_content . "\n";

	// Kiểm tra loại khối và thực hiện quá trình hợp nhất tương ứng
	if ($block_type == 'StrDictionary') {
		// Tách các dòng trong file1 vào mảng
		$file1_lines = explode("\n", $file1_content);
		$file1_keys = [];

		// Xử lý mỗi dòng để lấy key
		foreach ($file1_lines as $line) {
			$line = trim($line);
			if (!empty($line)) {
				$fields = explode("\t", $line);
				if (isset($fields[1])) {
					// Sử dụng trường thứ hai làm key và lưu vào mảng
					$file1_keys[$fields[1]] = true;
				}
			}
		}

		// Tạo tập hợp từ mảng keys
		$file1_blocks_set = array_flip(array_keys($file1_keys));

		// Tách các dòng trong file2 và kiểm tra
		$file2_lines = explode("\n", $file2_content);
		foreach ($file2_lines as $line) {
			$line = trim($line);
			if (!empty($line)) {
				$fields = explode("\t", $line);
				$key = $fields[1]; // Lấy trường thứ 2 làm key

				// Kiểm tra xem trường đã tồn tại trong tập hợp file1 chưa
				if (!isset($file1_blocks_set[$key]) && !isset($processed_blocks[$key])) {
					// Nếu không tồn tại, thêm vào nội dung hợp nhất
					$merged_content .= $line . "\n";
					$processed_blocks[$key] = true; // Đánh dấu đã xử lý
					$log_content .= "Thêm dòng mới từ file 2: $key\n";
				} else {
					$log_content .= "{$key} ---Trùng lặp bỏ qua.\n";
				}
			}
		}

		// Ghi lại nội dung mới của file1 sau khi hợp nhất
		//file_put_contents($merged_file_path, $merged_content);
		// Ghi log nếu cần
		//file_put_contents($log_file_path, $log_content);
	}
	elseif ($block_type == 'obj') {
		$OBJ_pattern = '/<Object\s+name="([^"]+)"/';
		$log_file = fopen($log_file_path, 'w');
		$file1_blocks_set = array_flip($file1_blocks); // Sử dụng array_flip ở đây

		// Thực hiện kết hợp khớp và lưu vào $matches
		if (preg_match_all($OBJ_pattern, $file2_content, $matches)) {
			foreach ($matches[1] as $OBJ_name) {
				// Sử dụng isset thay vì in_array cho việc kiểm tra sự tồn tại
				if (!isset($file1_blocks_set[$OBJ_name]) && !isset($processed_blocks[$OBJ_name])) {
					// Các bước tìm kiếm chuỗi và trích xuất nội dung giống như trước
					$start_index = strpos($file2_content, '<Object name="' . $OBJ_name . '"');
					if ($start_index !== false) {
						$end_index = strpos($file2_content, '</Object>', $start_index);
						if ($end_index !== false) {
							$length = $end_index - $start_index + strlen('</Object>');
							$merged_content .= substr($file2_content, $start_index, $length) . "\n";
							$processed_blocks[$OBJ_name] = true;
							$log_content .= "{$OBJ_name} ---Hợp nhất thành công.\n";
						}
					}
				} else {
					$log_content .= "{$OBJ_name} ---Trùng lặp bỏ qua.\n";
				}
			}
		} else {
			// Xử lý trường hợp không tìm thấy khớp nào
		}

		// Đừng quên ghi log_content vào log_file trước khi đóng
		//fwrite($log_file, $log_content);
		fclose($log_file);
	}
	elseif ($block_type == 'particle') {
		$log_file = fopen($log_file_path, 'w');
		// và $file2_content đã được đọc sẵn

		// Chuyển đổi mảng $file1_blocks thành một tập hợp (set) để tối ưu việc tìm kiếm
		$file1_blocks_set = array_flip($file1_blocks);

		// Duyệt qua từng khối trong file2
		foreach ($file2_blocks as $block_name) {
			// Kiểm tra sự tồn tại và trùng lặp của khối
			if (!isset($file1_blocks_set[$block_name]) && !isset($processed_blocks[$block_name])) {
				// Lấy nội dung của khối từ file 2 bằng cách đọc từng dòng
				$pattern = '/^\s*' . preg_quote($block_name) . '\s*\{.*?^\}/ms';
				if (preg_match($pattern, $file2_content, $block_content)) {
					// Thêm nội dung của khối vào chuỗi hợp nhất
					$merged_content .= $block_content[0] . "\n";
					// Đánh dấu khối này đã được xử lý
					$processed_blocks[$block_name] = true;
					// Ghi log cho quá trình hợp nhất khối
					$log_content .= "{$block_name} ---Hợp nhất thành công.\n";
				}
			} else {
				// Xử lý trường hợp khối trùng lặp
				$log_content .= "{$block_name} ---Trùng lặp bỏ qua.\n";
			}
		}
		fclose($log_file);
	}
	else {//array('skill', 'material', 'effect')
		$log_file = fopen($log_file_path, 'w');

		$block_pattern = "/($block_type\s+([^\s]+)(\s*:\s*[^\s]+)?\s*\{[\s\S]*?\n\})/";
		$file1_blocks_set = array_flip($file1_blocks); // Đổi này để sử dụng array_flip

		// Thực hiện kết hợp khớp và lưu vào $matches
		if (preg_match_all($block_pattern, $file2_content, $matches)) {
			foreach ($matches[2] as $index => $block_name) { // Đổi này để truy xuất tên khối từ $matches
				if (!isset($file1_blocks_set[$block_name]) && !isset($processed_blocks[$block_name])) {
					$merged_content .= $matches[0][$index] . "\n";
					$processed_blocks[$block_name] = true;
					$log_content .= "{$block_name} ---Hợp nhất thành công.\n";
				} else {
					$log_content .= "{$block_name} ---Trùng lặp bỏ qua.\n";
				}
			}
		} else {
			$log_content .= "Không có Khối nào hợp lệ.\n";
		}

		// Ghi log nếu cần
		//file_put_contents($log_file_path, $log_content);

		fclose($log_file);
	}

	// Ghi nội dung đã hợp nhất vào File mới
	$output_file_path = 'merged_' . $block_type . '.txt';
	file_put_contents($output_file_path, $merged_content);

}

// Hàm để fix id stt
function FixSTT_files($file1_path, $block_type, &$log_content) {
    // Đọc nội dung từ file
    $file_content = file_get_contents($file1_path);

    // Tách nội dung thành từng dòng
    $file_lines = explode("\n", $file_content);

    // Duyệt qua từng dòng và sửa lại trường đầu tiên
    foreach ($file_lines as $index => &$line) {
        // Bỏ qua dòng trống
        if (!empty($line)) {
            // Tách dòng thành các trường, sử dụng dấu tab "\t" để tách
            $fields = explode("\t", $line);
            
            // Kiểm tra xem dòng có ít nhất 1 trường không
            if (!empty($fields[0])) {
                // Chuyển trường đầu tiên sang kiểu số và tăng giá trị lên 1
                $IdNew = $index + 1; // Số thứ tự dòng + 1
                $log_content .= "{$fields[0]} ->>> {$IdNew} ---- {$fields[1]} ---Fix STT thành công.\n";

                // Gắn lại các trường vào dòng
                $fields[0] = $IdNew;
                $line = implode("\t", $fields);
            }
        }
    }

    // Ghi nội dung đã fix số thứ tự vào File mới
    $output_file_path = 'Fix_STT_' . $block_type . '.txt';
    file_put_contents($output_file_path,implode("\n", $file_lines));
}


// Hàm để fix
function Fix_all_files($file1_path, $file2_path, $block_type, &$log_content) {
    // Kiểm tra xem loại khối được chọn có hợp lệ không
    $valid_block_types = array('Check_Fix_allskill', 'Fix_loi_khoi_particle', 'Extract_allmaterial', 'Check_Fix_allmaterial', 'Check_Fix_alleffect', 'Check_Fix_allparticle','Fix_allskill','Fix_alleffect', 'Fix_allparticle', 'Check_Fix_mesh_skeleton_IMG');
    if (!in_array($block_type, $valid_block_types)) {
        // Thông báo lỗi cho người dùng
        $log_content .= "Loại khối không hợp lệ.\n";
        return;
    }
	//var_dump($block_type);
    // Đọc nội dung từ file 1
    $file1_content = file_get_contents($file1_path);

    // Khởi tạo biến để lưu nội dung cần ghi vào file mới
    $content_txt = '';

	// Khởi tạo mảng để lưu trữ thông tin về các khối đã được xử lý
    $processed_blocks = array();

    if ($block_type == 'Fix_loi_khoi_particle') {
		// Pattern để tìm và trích xuất tên của các khối
		$block_pattern = '/^\s*([^\s\{]+)\s*\{.*?\}/ms';

		// Tìm và trích xuất các khối từ file1
		if (preg_match_all($block_pattern, $file1_content, $matches)) {
			foreach ($matches[1] as $block_name) {
				
			//	var_dump($block_name);
				// Kiểm tra nếu khối đã được xử lý
				if (!isset($processed_blocks[$block_name])) {
					// Trích xuất toàn bộ nội dung của khối
					$full_block = extractFullBlock($file1_content, $block_name);
					// Sử dụng biểu thức chính quy để kiểm tra xem khối có chứa cặp dấu {} không
					if (preg_match('/^\s*' . preg_quote($block_name) . '\s*\{.*?\}/ms', $full_block, $______)) {
						// Thêm nội dung của khối vào biến $content_txt
						$content_txt .= $full_block . "\n\n";
						// Đánh dấu khối đã được xử lý
						$processed_blocks[$block_name] = true;
						$log_content .= "{$block_name} --- Extract block thành công.\n";
					}
					else {
						// Biểu thức chính quy tìm khối kết thúc bằng dấu } và sau đó là tên khối
						$block_pattern_corrected = '/}([^{]*?)\b' . preg_quote($block_name) . '\b\s*{/s';

						// Tìm tất cả các khối từ đầu file
						if (preg_match_all($block_pattern_corrected, $file1_content, $corrected_matches)) {
							foreach ($corrected_matches[1] as $index => $content_before_block) {
								// Kiểm tra xem có phải là nội dung trước khối $block_name đầu tiên không lỗi
								if (strpos($content_before_block, "{") === false) {
									// Nếu tên $block_name xuất hiện sau dấu } và trước dấu { của khối tiếp theo
									// Đánh dấu vị trí bắt đầu của khối không lỗi
									$start_of_correct_block = strpos($file1_content, $corrected_matches[0][$index]);							
									// Trích xuất khối không lỗi
									$correct_block = extractFullBlock(substr($file1_content, $start_of_correct_block, strlen($file1_content)), $block_name);
									// Thêm nội dung của khối vào biến $content_txt
									$content_txt .= trim($correct_block) . "\n\n";
									// Đánh dấu khối đã được xử lý
									$processed_blocks[$block_name] = true;
									$log_content .= "{$block_name} --- Extract block không lỗi thành công.\n";
									
									// Tìm và xử lý một khối không lỗi đầu tiên, nên có thể thoát khỏi vòng lặp sau đó
									break;
								}
							}
						} else {
							// Nếu không tìm thấy khối không lỗi, ghi log lỗi
							$log_content .= "{$block_name} --- Không tìm thấy khối không lỗi sau dấu }.\n";
						}
					}
				}
			}
		}

	}
    elseif ($block_type == 'Extract_allmaterial') {
		$content_txt_img = '';

		// Đọc tên khối từ file2
		$file2_blocks = file($file2_path, FILE_IGNORE_NEW_LINES);
		$file2_blocks = array_map('trim', $file2_blocks); // Chuẩn hóa dữ liệu đầu vào

		// Pattern để tìm khối có tên và nội dung trong $file1_content
		$block_type_escaped = preg_quote('material', '/');
		$block_pattern = "/($block_type_escaped\s+([^\s]+)(\s*:\s*[^\s]+)?\s*\{[\s\S]*?\n\})/";

		// Thực hiện kết hợp khớp và lưu vào $matches
		if (preg_match_all($block_pattern, $file1_content, $matches, PREG_SET_ORDER)) {
			foreach ($matches as $match) {
				$block_name = $match[2]; // Tên của khối
				if (in_array($block_name, $file2_blocks) && !isset($processed_blocks[$block_name])) {
					$content_txt .= $match[0] . "\n\n"; // Thêm toàn bộ khối vào nội dung
					$processed_blocks[$block_name] = true; // Đánh dấu đã xử lý
					$log_content .= "{$block_name} --- Extract block thành công.\n";

					// Duyệt qua từng dòng trong khối để tìm thông tin hình ảnh
					$lines = explode("\n", $match[0]);
					foreach ($lines as $line) {
						// Sử dụng biểu thức chính quy để tìm kiếm các dòng có thông tin hình ảnh
						if (preg_match('/set_texture_alias <[^>]+> (\S+)/', $line, $img_matches) || preg_match('/texture (\S+)/', $line, $img_matches)) {
							$fileName = $img_matches[1];
							// Kiểm tra xem $fileName có chứa '/' hay không
							if (strpos($fileName, '/') !== false) {
								// Tách $fileName thành mảng dựa trên '/'
								$parts = explode('/', $fileName);
								// Lấy phần cuối cùng của đường dẫn làm tên file và phần trước đó là thư mục
								$fileName = array_pop($parts);
								$directoryName = implode('/', $parts);
								// Ghi vào mảng với format mong muốn
								$formattedKey = $fileName . "\t" . $directoryName;
							} else {
								// Trường hợp $fileName không chứa '/', ghi trực tiếp không cần bóc tách
								$formattedKey = $fileName;
							}

							// Thêm vào nội dung chỉ nếu chưa được xử lý
							if (!array_key_exists($formattedKey, $processed_blocks)) {
								$content_txt_img .= $formattedKey . "\n";
								$log_content .= $formattedKey . " -> ghi lại thành công.\n";
								$processed_blocks[$formattedKey] = true;
							}
						}
					}
				}
			}

			// Tên của file mới dựa trên loại khối
			$output_file_path_img = "output_{$block_type}_IMG.txt";

			// Ghi nội dung đã thu thập vào file mới
			file_put_contents($output_file_path_img, $content_txt_img);
		} else {
			// Xử lý trường hợp không tìm thấy khối nào hợp lệ
			$log_content .= "Không có khối nào hợp lệ.\n";
		}
	}
    elseif($block_type == 'Check_Fix_allmaterial') {
		// Duyệt qua từng dòng trong file
		$file1_lines = explode("\n", $file1_content);
		foreach ($file1_lines as $line) {
			// Kiểm tra các loại thông báo lỗi về việc không thể gán vật liệu và xử lý tương ứng
			if (strpos($line, "Can't assign material") !== false) {
				preg_match("/Can't assign material (.*?) to SubEntity/", $line, $matches);
				if (!empty($matches[1]) && !isset($processed_blocks[$matches[1]])) {
					$material_name = $matches[1]; // Lấy tên vật liệu
					$content_txt .= $material_name . "\n"; // Thêm tên vật liệu vào nội dung cần ghi
					$log_content .= "{$material_name} ->>> ghi lại thành công.\n"; // Log (nếu cần)
					$processed_blocks[$material_name] = true; // Đánh dấu đã xử lý
				}
			}
		}

		//var_dump($log_content);

    }
    elseif($block_type == 'Check_Fix_allparticle') {
		// Duyệt qua từng dòng trong file1
		$file1_lines = explode("\n", $file1_content);
		foreach ($file1_lines as $line) {
			// Kiểm tra các loại thông báo lỗi và xử lý tương ứng
			if (strpos($line, 'Bad particle affector attribute line') !== false) {
				preg_match_all("/'(.*?)'/", $line, $matches);
				foreach ($matches[1] as $match) {
					if (!isset($processed_blocks[$match])) {
						$content_txt .= $match . "\n";
						$log_content .= "{$match} ->>> ghi lại thành công ----1.\n";
						$processed_blocks[$match] = true;
					}
				}
			} 
			elseif (strpos($line, 'Bad particle system attribute line') !== false || strpos($line, "Particle system affector doesn't exists at line") !== false) {
				preg_match("/'([^']*)' in ([^(]*)(?: \(tried renderer\))?/", $line, $matches);
				// Xác định nếu dòng hiện tại có chứa "(tried renderer)"
				$rendererText = strpos($line, "(tried renderer)") !== false ? "\t(tried renderer)" : "";
				// Sử dụng $matches[1] kết hợp với trạng thái của $rendererText để tạo key duy nhất
				// fix 
				$matches[2] = substr($matches[2], 0, -1); // Lấy chuỗi từ đầu và bỏ đi chữ cuối cùng
				if ($matches[2]) {
					$key = $matches[1] . $matches[2] . $rendererText;
				} else {
					$key = $matches[1] . $rendererText;
				}
				if (!empty($matches) && !isset($processed_blocks[$key])) {
					// Ghi vào biến content_txt dưới dạng khối
					$content_txt .= "{$matches[1]}\t{$matches[2]}{$rendererText}\n";
					$id = 2;
					if ($rendererText !== '') {
						$id = 3;
					}
					
					$log_content .= "{$matches[1]} in {$matches[2]}{$rendererText} ->>> ghi lại thành công ----$id.\n";
					$processed_blocks[$key] = true;
				}
			}
			elseif (strpos($line, 'Bad particle emitter attribute line') !== false) {
				// Sửa đổi biểu thức chính quy để lấy chính xác nội dung cần thiết
				preg_match("/'([^']*)' for ([^'\n]*)/", $line, $matches);
				
				// Tạo key duy nhất dựa trên nội dung và tên emitter
				$key = "{$matches[1]}_{$matches[2]}";

				if (!empty($matches) && !isset($processed_blocks[$key])) {
					// Ghi vào biến content_txt dưới dạng khối
					$content_txt .= "{$matches[1]}\t{$matches[2]}\n";
					$log_content .= "{$matches[1]} for {$matches[2]} ->>> ghi lại thành công ----4.\n";
					$processed_blocks[$key] = true;
				}
			}
		}

		//var_dump($log_content);

    }
    elseif ($block_type == 'Check_Fix_mesh_skeleton_IMG') {
		// Duyệt qua từng dòng trong file1
		$file1_lines = explode("\n", $file1_content);
		foreach ($file1_lines as $line) {
			if (strpos($line, 'Description: Unable to load skeleton') !== false) {
				// Biểu thức chính quy để bắt tên file .skeleton trước cụm từ "for mesh"
				if (preg_match('/Unable to load skeleton (\S+\.skeleton)/', $line, $matches)) {
					$fileName = $matches[1];
					// Kiểm tra xem $fileName có chứa '/' hay không
					if (strpos($fileName, '/') !== false) {
						// Tách $fileName thành mảng dựa trên '/'
						$parts = explode('/', $fileName);
						// Lấy phần cuối cùng của đường dẫn làm tên file và phần trước đó là thư mục
						$fileName = array_pop($parts);
						$directoryName = implode('/', $parts);
						// Ghi vào mảng với format mong muốn
						$formattedKey = $fileName;// . "\t" . $directoryName;
					} else {
						// Trường hợp $fileName không chứa '/', ghi trực tiếp không cần bóc tách
						$formattedKey = $fileName;
					}
					if (!isset($processed_blocks[$formattedKey])) {
						$content_txt .= $formattedKey . "\n";
						$log_content .= $formattedKey . " -> ghi lại thành công.\n";
						$processed_blocks[$formattedKey] = true;
					}
				}
			} elseif (strpos($line, 'Description: Unable to derive resource group for') !== false || strpos($line, 'Description: Cannot locate resource') !== false || strpos($line, 'Description: Unable to load mesh') !== false) {
				// Biểu thức chính quy để bắt tên file .mesh trước cụm từ "in resource group"
				if (preg_match('/Unable to derive resource group for (\S+\.mesh)/', $line, $matches) || preg_match('/Unable to derive resource group for (\S+\.skeleton)/', $line, $matches) || preg_match('/Cannot locate resource (\S+\.skeleton)/', $line, $matches) || preg_match('/Cannot locate resource (\S+\.mesh)/', $line, $matches) || preg_match('/Unable to load mesh (\S+\.mesh)/', $line, $matches)) {
					$fileName = $matches[1];
					// Kiểm tra xem $fileName có chứa '/' hay không
					if (strpos($fileName, '/') !== false) {
						// Tách $fileName thành mảng dựa trên '/'
						$parts = explode('/', $fileName);
						// Lấy phần cuối cùng của đường dẫn làm tên file và phần trước đó là thư mục
						$fileName = array_pop($parts);
						$directoryName = implode('/', $parts);
						// Ghi vào mảng với format mong muốn
						$formattedKey = $fileName . "\t" . $directoryName;
					} else {
						// Trường hợp $fileName không chứa '/', ghi trực tiếp không cần bóc tách
						$formattedKey = $fileName;
					}
					if (!isset($processed_blocks[$formattedKey])) {
						$content_txt .= $formattedKey . "\n";
						$log_content .= $formattedKey . " -> ghi lại thành công.\n";
						$processed_blocks[$formattedKey] = true;
					}
				}
				elseif (preg_match('/Cannot locate resource (\S+\.model)/', $line, $matches)) {
					$fileName = $matches[1];
					// Kiểm tra xem $fileName có chứa '/' hay không
					if (strpos($fileName, '/') !== false) {
						// Tách $fileName thành mảng dựa trên '/'
						$parts = explode('/', $fileName);
						// Lấy phần cuối cùng của đường dẫn làm tên file và phần trước đó là thư mục
						$fileName = array_pop($parts);
						$directoryName = implode('/', $parts);
						// Ghi vào mảng với format mong muốn
						$formattedKey = $fileName . "\t" . $directoryName;
					} else {
						// Trường hợp $fileName không chứa '/', ghi trực tiếp không cần bóc tách
						$formattedKey = $fileName;
					}
					if (!isset($processed_blocks[$formattedKey])) {
						$content_txt .= $formattedKey . "\n";
						$log_content .= $formattedKey . " -> ghi lại thành công.\n";
						$processed_blocks[$formattedKey] = true;
					}
				}
				elseif (preg_match('/Cannot locate resource (\S+\.jpg)/', $line, $matches) || preg_match('/Cannot locate resource (\S+\.dds)/', $line, $matches) || preg_match('/Cannot locate resource (\S+\.png)/', $line, $matches)) {
					$fileName = $matches[1];
					// Kiểm tra xem $fileName có chứa '/' hay không
					if (strpos($fileName, '/') !== false) {
						// Tách $fileName thành mảng dựa trên '/'
						$parts = explode('/', $fileName);
						// Lấy phần cuối cùng của đường dẫn làm tên file và phần trước đó là thư mục
						$fileName = array_pop($parts);
						$directoryName = implode('/', $parts);
						// Ghi vào mảng với format mong muốn
						$formattedKey = $fileName . "\t" . $directoryName;
					} else {
						// Trường hợp $fileName không chứa '/', ghi trực tiếp không cần bóc tách
						$formattedKey = $fileName;
					}
					if (!isset($processed_blocks[$formattedKey])) {
						$content_txt .= $formattedKey . "\n";
						$log_content .= $formattedKey . " -> ghi lại thành công.\n";
						$processed_blocks[$formattedKey] = true;
					}
				}
			}
		}


		//var_dump($block_type);

    }
	elseif ($block_type == 'Fix_allparticle') {
		// Đọc nội dung từ file 2
		$file2_content = file_get_contents($file2_path);

		// Tách các dòng trong file 2
		$file2_lines = explode("\n", $file2_content);

		// Tạo một tập hợp (set) để lưu trữ các dòng trong file 2
		$file2_set = array_flip($file2_lines);

		// Định nghĩa các loại khối con cần tìm
		$___Types = ["emitter Ring", "emitter Point"];

		// Duyệt qua từng dòng trong file 2
		foreach ($file2_set as $line => $value) {
			// Kiểm tra xem dòng có trong danh sách đã xử lý chưa
			//var_dump('$line'.$line);

			// Kiểm tra số lượng khối trong dòng
			$blocks = explode("\t", $line);
			$num_blocks = count($blocks);
				
			$___key = $blocks[0];
			if ($num_blocks === 2) {
				$___key = $blocks[0] . $blocks[1];
			}
			elseif ($num_blocks === 3) {
				$___key = $blocks[0] . $blocks[1] . $blocks[2];
			}
				//var_dump('$blocks  '.$blocks);
				//$log_content .= $num_blocks . '\n';
			//kiểm tra $___key đã xử lý hay chưa nếu chưa xử lý thì chạy
			if (!isset($processed_blocks[$___key])) {
				
				if ($num_blocks === 1) {
					// Xử lý dòng chỉ có 1 khối
					// Kiểm tra xem dòng trong file 2 có tồn tại trong file 1 không
					if (strpos($file1_content, $line) !== false) {
						// Nếu dòng trong file 2 tồn tại trong file 1, thay thế từ trùng lặp bằng chuỗi rỗng trong file 1
						$file1_content = str_ireplace($line, '', $file1_content);
						// Ghi log
						$log_content .= "{$line} ->>> đã được xoá thành công ----$num_blocks.\n";
					}
				}
				elseif ($num_blocks === 2) {
					// Lấy tên khối cha từ mảng $blocks
					$parent_block = $blocks[1];

					// kiểm tra xem khối trong khối cha tồn tại hay không
					$emitter_position = strpos($parent_block, "emitter");
					if ($emitter_position !== false) {

						// Lấy ra các khối cha có chứa khối con $___Types
						$________Blocks = extractParentBlocksWiths($file1_content, $___Types);

						foreach ($________Blocks as $parent_content) {
							// Giả sử $blocks[0] là tên của khối con cần thay thế từ trùng lặp
							$child_block = $blocks[0]; // Cần được định nghĩa cụ thể
							
							if (strpos($parent_content, $child_block) !== false) {
								// Thay thế từ trùng lặp với $child_block thành rỗng
								$updated_parent_content = str_ireplace($child_block, '', $parent_content);

								// Xoá khối cha bao gồm các khối con bên trong từ $file1_content
								// Đây là bước phức tạp vì bạn cần xác định vị trí chính xác của khối cha trong $file1_content để xoá
								// Có thể cần viết một hàm riêng để xử lý việc này

								// Xoá khối cha bao gồm các khối con bên trong
							//	removeParentBlock($file1_content, getParentBlockNames($parent_content));
								// Thêm khối đã sửa vào cuối của $file1_content
							//	$file1_content .= "\n" . $updated_parent_content;
								
								// Ghi log
								$log_content .= $parent_content;
							//	$log_content .= "Khối cha với khối con đã được cập nhật thành công.\n";
							}
						}


					}
					else {
					// Trích xuất khối cha
					$parent_content = extractFullBlock($file1_content, $parent_block);
					if ($parent_content !== false) {
						// Lấy tên khối con, giả sử bạn muốn xóa khối con này
						$child_block = $blocks[0];

						// Xóa khối con khỏi nội dung khối cha
						removeChildBlock($parent_content, $child_block);
		 //file_put_contents('123.txt', $parent_content);
						// Ghi log
						$log_content .= "{$parent_block} ->>> ghi lại thành công ----$num_blocks.\n";

						// Thay thế khối cha cũ trong nội dung file bằng khối cha đã được cập nhật
						$escapedParentBlock = preg_quote($parent_block, '/');
						// Cố gắng khớp mở đầu và kết thúc khối một cách chính xác hơn
						$pattern = "/$escapedParentBlock\s*\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}/s";

						// Đảm bảo chuỗi thay thế không có ký tự escape thừa
						$replacement = str_replace('$', '\$', $parent_content); // Escape dấu $ nếu có
						$file1_content = preg_replace($pattern, $replacement, $file1_content, 1);

					}
					
					}
				}
//$log_content .=  "$num_blocks ------  $line\n";
		//$log_content .=  "------[$blocks[0]]--------[$blocks[2]]---------[$parent_block]\n";
				elseif ($num_blocks === 3) {
					// Lấy nội dung khối cha và chuẩn bị thay thế
					$parent_block = $blocks[1]; // Lấy tên khối cha từ mảng $blocks
					$parent_content = extractFullBlock($file1_content, $parent_block); // Trích xuất khối cha
					
				//	var_dump($file1_content);
				//	var_dump($blocks[2]);
					if ($parent_content !== false && isset($blocks[2]) && $blocks[2] === "(tried renderer)") {
						$child_block = $blocks[0]; // Lấy tên khối con
				//	var_dump($child_block);

						if (strpos($parent_content, $child_block) !== false) {
							// Thay thế từ trùng lặp với $child_block thành rỗng
							$updated_parent_content = str_ireplace($child_block, '', $parent_content);
							// Xoá khối cha bao gồm các khối con bên trong
							removeParentBlock($file1_content, $parent_block);
							// Thêm khối đã sửa vào cuối của $file1_content
							$file1_content .= "\n" . $updated_parent_content; // Thêm dấu xuống dòng trước khối mới để tách biệt
							
							// Ghi log
							$log_content .= "{$parent_block} ->>> đã được cập nhật thành công ----$num_blocks.\n";
						}
					} else {
						// Ghi log
						$log_content .= "{$parent_block} ->>> không thể lấy ra khối ----$num_blocks.\n";
					}

				}
				// Thêm dòng đã xử lý vào mảng
				$processed_blocks[$___key] = true;
			}
		}
		// Sau khi đã thực hiện tất cả các thay thế:
		//removeDuplicateParentBlocks($file1_content);
		// Ghi nội dung đã xử lý vào biến $content_txt
		$content_txt = $file1_content;

    }
	elseif ($block_type == 'Fix_alleffect' || $block_type == 'Fix_allskill') {
		// Đọc nội dung từ file 2
		$file2_content = file_get_contents($file2_path);

		// Tách các dòng trong file 2
		$file2_lines = explode("\n", $file2_content);

		// Duyệt qua từng dòng trong file 2
		foreach ($file2_lines as $line) {
			var_dump($line);
			// Kiểm tra xem dòng trong file 2 có tồn tại trong file 1 không
			if (strpos($file1_content, $line) !== false) {
				// Nếu dòng trong file 2 tồn tại trong file 1, thay thế từ trùng lặp bằng chuỗi rỗng trong file 1
				$file1_content = str_ireplace($line, '', $file1_content);
				// Ghi log
				$log_content .= "{$line} ->>> đã được xoá thành công .\n";
			}
		}
		// Ghi nội dung đã xử lý vào biến $content_txt
		$content_txt = $file1_content;

    }
	elseif ($block_type == 'Check_Fix_alleffect' || $block_type == 'Check_Fix_allskill' ) {
		// Tách nội dung thành các phần bằng dấu xuống dòng
		$lines = explode("\n", $file1_content);
		$processingEffectFile = false;

		foreach ($lines as $line) {
			// Kiểm tra nếu dòng bắt đầu bằng "file :" để xác định đang xử lý file nào
			if (strpos($line, 'file :') !== false) {
				// Nếu không phải là file all.effect, bỏ qua các line content cho đến khi gặp file all.effect tiếp theo
				$processingEffectFile = false;
				// Nếu là file all.effect, bắt đầu xử lý các line content tiếp theo
				if ($block_type == 'Check_Fix_alleffect' && strpos($line, 'all.effect') !== false) {
					$processingEffectFile = true;
				} elseif ($block_type == 'Check_Fix_allskill' && strpos($line, 'all.skill') !== false) {
					$processingEffectFile = true;
				}
			} elseif ($processingEffectFile && strpos($line, 'line content :') !== false) {
				// Nếu đang xử lý file all.effect và gặp dòng chứa "line content :"
				$lineContent = trim(str_replace('line content :', '', $line));
				// Kiểm tra nếu nội dung chưa được xử lý
				if (!isset($processed_blocks[$lineContent])) {
					// Lưu nội dung đã xử lý
					$content_txt .= $lineContent . "\n";
					$log_content .= "[{$lineContent}] ->>> ghi lại thành công.\n";
					$processed_blocks[$lineContent] = true;
				}
			}
		}
    }

    // Tên của file mới dựa trên loại khối
    $output_file_path = "output_{$block_type}.txt";

    // Ghi nội dung đã thu thập vào file mới
    file_put_contents($output_file_path, $content_txt);
}

// Hàm tìm kiếm file trong thư mục và các thư mục con
function findFileInDirectory($directory, $fileName) {
    $iterator = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($directory, RecursiveDirectoryIterator::SKIP_DOTS),
        RecursiveIteratorIterator::SELF_FIRST
    );
    foreach ($iterator as $file) {
        if ($file->isFile() && $file->getFilename() === $fileName) {
            return $file->getPathname();
        }
    }
    return false;
}

// Hàm lấy ra tên khối cha
function getParentBlockNames($blocks) {

    // Tìm các khối trong nội dung và lưu vào mảng
    preg_match_all('/^\s*([^\s\{]+)\s*\{/m', $blocks, $matches, PREG_SET_ORDER);

    // Khởi tạo mảng để lưu trữ các khối duy nhất
    $blocks = array();
    foreach ($matches as $match) {
        // Thêm tên khối vào mảng
        $blocks[] = $match[1];
    }
    $blocks = array_unique($blocks);
	
    // Ghi tên các khối vào file log
    foreach ($blocks as $block) {
        $blocks .= $block . "\n";
    }
	return $blocks;
}
// Hàm lấy ra tất cả các khối cha có chứa ít nhất một trong các khối con $____Types 
function extractParentBlocksWiths($content, $emitterTypes) {
    $parentBlocks = [];
    $pattern = '/\b(\w+)\s*\{(.*?)\}\s*(?=\w+\s*\{|$)/s';

    preg_match_all($pattern, $content, $matches, PREG_SET_ORDER);

    foreach ($matches as $match) {
        $blockContent = $match[0]; // Toàn bộ nội dung khối, bao gồm tên khối và nội dung bên trong ngoặc nhọn
        // Kiểm tra từng loại khối con được chỉ định xem có xuất hiện trong nội dung khối này không
        foreach ($emitterTypes as $type) {
            if (strpos($blockContent, $type) !== false) {
                // Nếu khối cha có chứa ít nhất một trong các khối con cần tìm, lưu toàn bộ nội dung khối vào mảng kết quả
                $parentBlocks[] = $blockContent;
                // Không cần dừng việc kiểm tra vì muốn lấy tất cả khối đạt yêu cầu
            }
        }
    }

    return $parentBlocks;
}

// Hàm này xóa khối cha và tất cả khối con bên trong dựa trên tên khối cha
function removeParentBlock(&$content, $blockName) {
    while (true) {
        // Tìm vị trí bắt đầu của tên khối cha trong nội dung
        $blockStartPos = strpos($content, $blockName);
        if ($blockStartPos === false) {
            return; // Tên khối cha không được tìm thấy, thoát vòng lặp
        }

        // Kiểm tra xem tên khối cha có phải là một từ độc lập không
        if (($blockStartPos != 0 && ctype_alpha($content[$blockStartPos - 1])) ||
            (isset($content[$blockStartPos + strlen($blockName)]) && ctype_alpha($content[$blockStartPos + strlen($blockName)]))) {
            return; // Tên khối cha không phải là một từ độc lập
        }

        $braceCount = 0;
        $inBlock = false;
        for ($i = $blockStartPos; $i < strlen($content); $i++) {
            if ($content[$i] == '{') {
                $braceCount++;
                if (!$inBlock) {
                    // Đánh dấu vị trí bắt đầu của khối
                    $inBlock = true;
                    $blockStart = $i;
                }
            } elseif ($content[$i] == '}') {
                $braceCount--;
                if ($braceCount == 0 && $inBlock) {
                    // Đánh dấu vị trí kết thúc của khối và thoát vòng lặp
                    $blockEnd = $i + 1; // Bao gồm cả dấu ngoặc đóng
                    break;
                }
            }
        }

        if ($inBlock && isset($blockEnd)) {
            // Xóa toàn bộ khối cha, từ tên đến nội dung
            $content = substr_replace($content, '', $blockStartPos, $blockEnd - $blockStartPos);
        } else {
            // Nếu không tìm thấy cặp ngoặc đóng hợp lệ, thoát khỏi vòng lặp
            break;
        }
    }
}


//chua su dung dc
// Hàm này tìm và xoá khối cha trùng lặp, xoá khối trước và giữ lại khối cùng tên cuối cùng
function removeDuplicateParentBlocks(&$content) {
    $pattern = '/\b(\w+)\s*\{(?:[^{}]|\{(?>[^{}]+|(?-1))*\})*\}/m';
    preg_match_all($pattern, $content, $matches, PREG_OFFSET_CAPTURE | PREG_SET_ORDER);

    $lastOccurrence = [];

    // Xác định vị trí cuối cùng của mỗi khối
    foreach ($matches as $match) {
        $blockName = $match[1][0];
        $startPos = $match[0][1];
        $blockLength = strlen($match[0][0]);
        $endPos = $startPos + $blockLength;
        $lastOccurrence[$blockName] = ['start' => $startPos, 'end' => $endPos];
    }

    // Lật ngược mảng để bắt đầu từ khối cuối cùng
    foreach (array_reverse($matches, true) as $match) {
        $blockName = $match[1][0];
        $startPos = $match[0][1];
        $blockLength = strlen($match[0][0]);

        // Chỉ xoá nếu khối này không phải là lần xuất hiện cuối cùng
        if ($lastOccurrence[$blockName]['start'] != $startPos) {
            $content = substr_replace($content, '', $startPos, $blockLength);
        }
    }
}

// Hàm này tìm và trích xuất toàn bộ nội dung của khối cha, bao gồm cả khối con
function extractFullBlock($content, $blockName) {
    // Tìm vị trí bắt đầu của tên khối trong nội dung
    $blockStartPos = strpos($content, $blockName);
    var_dump($blockStartPos);
    if ($blockStartPos === false) {
        return false; // Tên khối không được tìm thấy
    }
    // Kiểm tra xem tên khối có phải là một từ độc lập không (tránh trường hợp tên khối là một phần của từ khác)
    if (($blockStartPos != 0 && ctype_alpha($content[$blockStartPos - 1])) || 
        (isset($content[$blockStartPos + strlen($blockName)]) && ctype_alpha($content[$blockStartPos + strlen($blockName)]))) {
        return false; // Tên khối không phải là một từ độc lập
    }

    $braceCount = 0;
    $inBlock = false;
    for ($i = $blockStartPos; $i < strlen($content); $i++) {
        if ($content[$i] == '{') {
            $braceCount++;
            if (!$inBlock) {
                // Đánh dấu vị trí bắt đầu của khối
                $inBlock = true;
                $blockStart = $i;
            }
        } elseif ($content[$i] == '}') {
            $braceCount--;
            if ($braceCount == 0 && $inBlock) {
                // Đánh dấu vị trí kết thúc của khối và thoát vòng lặp
                $blockEnd = $i;
                break;
            }
        }
    }

    if ($inBlock && isset($blockEnd)) {
        // Trả về toàn bộ khối từ tên đến nội dung
        return substr($content, $blockStartPos, $blockEnd - $blockStartPos + 1);
    } else {
        return false; // Không tìm thấy khối hợp lệ
    }
}

// Hàm này xóa khối con khỏi khối cha dựa trên tên khối con
function removeChildBlock(&$parentContent, $childBlockName) {
    // Tìm vị trí bắt đầu của tên khối con trong nội dung khối cha
    $blockStartPos = strpos($parentContent, $childBlockName);
    if ($blockStartPos === false) {
        return; // Tên khối con không được tìm thấy
    }
    
    // Kiểm tra xem tên khối con có phải là một từ độc lập không
    if (($blockStartPos != 0 && ctype_alpha($parentContent[$blockStartPos - 1])) || 
        (isset($parentContent[$blockStartPos + strlen($childBlockName)]) && ctype_alpha($parentContent[$blockStartPos + strlen($childBlockName)]))) {
        return; // Tên khối con không phải là một từ độc lập
    }

    $braceCount = 0;
    $inBlock = false;
    for ($i = $blockStartPos; $i < strlen($parentContent); $i++) {
        if ($parentContent[$i] == '{') {
            $braceCount++;
            if (!$inBlock) {
                // Đánh dấu vị trí bắt đầu của khối
                $inBlock = true;
                $blockStart = $i;
            }
        } elseif ($parentContent[$i] == '}') {
            $braceCount--;
            if ($braceCount == 0 && $inBlock) {
                // Đánh dấu vị trí kết thúc của khối và thoát vòng lặp
                $blockEnd = $i;
                break;
            }
        }
    }

    if ($inBlock && isset($blockEnd)) {
        // Xóa toàn bộ khối con, từ tên đến nội dung
        $parentContent = substr_replace($parentContent, '', $blockStartPos, $blockEnd - $blockStartPos + 1);
    }
}
// Xử lý khi người dùng gửi mẫu
if ($_SERVER["REQUEST_METHOD"] == "POST") {
	if (isset($_POST['MergeFiles'])) {
		// Lấy loại khối và đường dẫn tệp từ biểu mẫu
		$block_type = $_POST['block_type'];
		$file1_path = $_FILES['file1']['tmp_name'];
		// Kiểm tra xem $file1_path có giá trị null không
		if ($file1_path === null || $file1_path === '') {
			// Đường dẫn thư mục hiện tại
			$current_directory = dirname(__FILE__);

			// Tạo đường dẫn cho tệp tin mới
			$file1_path = $current_directory . DIRECTORY_SEPARATOR . 'merged_' . $block_type . '.txt';
			$file1_name = 'merged_' . $block_type . '.txt'; // Đường dẫn và tên file gốc
			$file1_directory = $current_directory; // Đường dẫn thư mục của file
		} else {

			$file1_name = $_FILES['file1']['name']; // Đường dẫn và tên file gốc
			$file1_directory = dirname($_FILES['file1']['tmp_name']); // Đường dẫn thư mục của file
		
		}
		// Kiểm tra xem tệp tin mới đã tồn tại chưa
		if (!file_exists($file1_path)) {
			// Nếu không tồn tại, thực hiện hành động tạo tệp tin ở đây
			// Ví dụ:
			file_put_contents($file1_path, ""); // Tạo một tệp tin rỗng
		}
		//var_dump($file1_path);
		
		$file2_path = $_FILES['file2']['tmp_name'];
		$file2_name = $_FILES['file2']['name']; // Đường dẫn và tên file gốc
		$file2_directory = dirname($_FILES['file2']['tmp_name']); // Đường dẫn thư mục của file
		if ($file2_path === null || $file2_path === '') {
			$log_content = 'Vui lòng nhập đầy đủ đường dẫn sau đó tiến hành Hợp nhất!';
		} else {
			$log_content = ""; // Biến để lưu trữ nội dung ghi chú
			
			$log_content .= "---Loại : {$block_type}\n\n---File 1: $file1_directory\ $file1_name\n---File 2: $file2_directory\ $file2_name\n\n\n";
			// Gọi hàm hợp nhất các tệp
			merge_files($file1_path, $file2_path, 'duplicate_blocks_log_' . $block_type . '.txt', $block_type, $log_content);
		}
	} elseif (isset($_POST['StrDictionary'])) {
	
		// Lấy loại khối và đường dẫn tệp từ biểu mẫu
		$block_type = $_POST['block_type'];
		$file1_path = $_FILES['file1']['tmp_name'];
		// Kiểm tra xem $file1_path có giá trị null không
		if ($file1_path === null || $file1_path === '') {
			// Đường dẫn thư mục hiện tại
			$current_directory = dirname(__FILE__);

			// Tạo đường dẫn cho tệp tin mới
			$file1_path = $current_directory . DIRECTORY_SEPARATOR . 'merged_' . $block_type . '.txt';
			$file1_name = 'merged_' . $block_type . '.txt'; // Đường dẫn và tên file gốc
			$file1_directory = $current_directory; // Đường dẫn thư mục của file
		} else {

			$file1_name = $_FILES['file1']['name']; // Đường dẫn và tên file gốc
			$file1_directory = dirname($_FILES['file1']['tmp_name']); // Đường dẫn thư mục của file
		
		}
		// Kiểm tra xem tệp tin mới đã tồn tại chưa
		if (!file_exists($file1_path)) {
			// Nếu không tồn tại, thực hiện hành động tạo tệp tin ở đây
			// Ví dụ:
			file_put_contents($file1_path, ""); // Tạo một tệp tin rỗng
		}
		//var_dump($file1_path);
		
		$file2_path = $_FILES['file2']['tmp_name'];
		$file2_name = $_FILES['file2']['name']; // Đường dẫn và tên file gốc
		$file2_directory = dirname($_FILES['file2']['tmp_name']); // Đường dẫn thư mục của file
		if ($file2_path === null || $file2_path === '') {
			$log_content = 'Vui lòng nhập đầy đủ đường dẫn sau đó tiến hành Hợp nhất!';
		} else {
			$log_content = ""; // Biến để lưu trữ nội dung ghi chú
			
			$log_content .= "---Loại : {$block_type}\n\n---File 1: $file1_directory\ $file1_name\n---File 2: $file2_directory\ $file2_name\n\n\n";

			// Gọi hàm hợp nhất các tệp
			merge_files($file1_path, $file2_path, 'duplicate_blocks_log_' . $block_type . '.txt', $block_type, $log_content);
        }

	} elseif (isset($_POST['Fix_STT'])) {
		//var_dump();
		// Lấy loại khối và đường dẫn tệp từ biểu mẫu
		$block_type = $_POST['block_type'];
		$file1_path = $_FILES['file1']['tmp_name'];
		// Kiểm tra xem $file1_path có giá trị null không
		if ($file1_path === null || $file1_path === '') {
			// Đường dẫn thư mục hiện tại
			$current_directory = dirname(__FILE__);

			// Tạo đường dẫn cho tệp tin mới
			$file1_path = $current_directory . DIRECTORY_SEPARATOR . 'merged_' . $block_type . '.txt';
			$file1_name = 'merged_' . $block_type . '.txt'; // Đường dẫn và tên file gốc
			$file1_directory = $current_directory; // Đường dẫn thư mục của file
		} else {

			$file1_name = $_FILES['file1']['name']; // Đường dẫn và tên file gốc
			$file1_directory = dirname($_FILES['file1']['tmp_name']); // Đường dẫn thư mục của file
		
		}

		$log_content = ""; // Biến để lưu trữ nội dung ghi chú
			
		$log_content .= "---Loại : {$block_type}\n\n---File 1: $file1_directory\ $file1_name\n\n\n";
	//	var_dump($log_content);
		// Gọi hàm hợp nhất các tệp
		FixSTT_files($file1_path, $block_type, $log_content);
		
	} elseif (isset($_POST['Fix_allskill']) || isset($_POST['Check_Fix_allskill']) || isset($_POST['Extract_allmaterial']) || isset($_POST['Check_Fix_allmaterial']) || isset($_POST['Check_Fix_mesh_skeleton_IMG']) || isset($_POST['Check_Fix_allparticle']) || isset($_POST['Check_Fix_alleffect']) || isset($_POST['Fix_allparticle']) || isset($_POST['Fix_alleffect'])) {
		//var_dump();
		// Lấy loại khối và đường dẫn tệp từ biểu mẫu
		$block_type = $_POST['block_type'];
		$file1_path = $_FILES['file1']['tmp_name'];
		// Kiểm tra xem $file1_path có giá trị null không
		if ($file1_path === null || $file1_path === '') {
			// Đường dẫn thư mục hiện tại
			$current_directory = dirname(__FILE__);

			// Tạo đường dẫn cho tệp tin mới
			$file1_path = $current_directory . DIRECTORY_SEPARATOR . 'output_' . $block_type . '.txt';
			$file1_name = 'output_' . $block_type . '.txt'; // Đường dẫn và tên file gốc
			$file1_directory = $current_directory; // Đường dẫn thư mục của file
		} else {

			$file1_name = $_FILES['file1']['name']; // Đường dẫn và tên file gốc
			$file1_directory = dirname($_FILES['file1']['tmp_name']); // Đường dẫn thư mục của file
		
		}
		$log_content = ""; // Biến để lưu trữ nội dung ghi chú
		$file2_path = '';
		$log_content .= "---Loại : {$block_type}\n\n---File 1: $file1_directory\ $file1_name\n\n\n";

		if (isset($_POST['Extract_allmaterial']) || isset($_POST['Fix_allparticle']) || isset($_POST['Fix_alleffect']) || isset($_POST['Fix_allskill'])) {
			$file2_path = $_FILES['file2']['tmp_name'];
			$file2_name = $_FILES['file2']['name']; // Đường dẫn và tên file gốc
			$file2_directory = dirname($_FILES['file2']['tmp_name']); // Đường dẫn thư mục của file
			if ($file2_path === null || $file2_path === '') {
				$log_content = 'Vui lòng nhập đầy đủ đường dẫn sau đó tiến hành Hợp nhất!';
			} else {
				$log_content .= "---File 2: $file2_directory\ $file2_name\n\n\n";
			}
		}
			
		//var_dump($_POST);
		// Gọi hàm hợp nhất các tệp
		Fix_all_files($file1_path, $file2_path, $block_type, $log_content);
	} elseif (isset($_POST['Fix_loi_khoi_particle'])) {
		// Lấy loại khối và đường dẫn tệp từ biểu mẫu
		$block_type = $_POST['block_type'];
		$file1_path = $_FILES['file1']['tmp_name'];
		// Kiểm tra xem $file1_path có giá trị null không
		if ($file1_path === null || $file1_path === '') {
			// Đường dẫn thư mục hiện tại
			$current_directory = dirname(__FILE__);

			// Tạo đường dẫn cho tệp tin mới
			$file1_path = $current_directory . DIRECTORY_SEPARATOR . 'output_' . $block_type . '.txt';
			$file1_name = 'output_' . $block_type . '.txt'; // Đường dẫn và tên file gốc
			$file1_directory = $current_directory; // Đường dẫn thư mục của file
		} else {

			$file1_name = $_FILES['file1']['name']; // Đường dẫn và tên file gốc
			$file1_directory = dirname($_FILES['file1']['tmp_name']); // Đường dẫn thư mục của file
		
		}
		$log_content = ""; // Biến để lưu trữ nội dung ghi chú
		$log_content .= "---Loại : {$block_type}\n\n---File 1: $file1_directory\ $file1_name\n\n\n";

			
		//var_dump($_POST);
		// Gọi hàm hợp nhất các tệp
		Fix_all_files($file1_path, '', $block_type, $log_content);
	}
}
?>
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Merge (Files) - TOOL TLBB</title>
    <link rel="stylesheet" href="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css">
    <style>
        .submit-button {
            margin-bottom: 10px;
        }
		.log-content {
			width: 100%;
			height: 500px;
			overflow: auto;
			border: 1px solid #ccc;
			padding: 5px 0 20px 0;
		}

    </style>
</head>
<body>
    <div class="container">
        <h1>Merge (Files) - TOOL TLBB CODE BY T_P</h1>
        <h5>TOOL này Merge + Fix File (Particle - Skill - Material - Effect - Object - StrDictionary) loại bỏ trùng lặp các Khối trong File</h5>
        <h5>Merge File 2 vào File 1 ----------- Chú ý File Log Encode in ANSI mới chạy được</h5>
        <form action="" method="post" enctype="multipart/form-data">
            <div class="form-group">
                <label for="block_type">Chọn loại:</label>
                <select class="form-control" name="block_type" id="block_type">
                    <option value="">---------------------------</option>
					<option value="Check_Fix_mesh_skeleton_IMG" <?php if (isset($block_type) && $block_type == 'Check_Fix_mesh_skeleton_IMG') echo 'selected'; ?>>---------Check ---Fix_mesh_skeleton_IMG---------</option>
					<option value="Check_Fix_allmaterial" <?php if (isset($block_type) && $block_type == 'Check_Fix_allmaterial') echo 'selected'; ?>>---------Check ---Fix_all.material---------</option>
					<option value="Extract_allmaterial" <?php if (isset($block_type) && $block_type == 'Extract_allmaterial') echo 'selected'; ?>>Extract_allmaterial</option>
					<option value="Check_Fix_allparticle" <?php if (isset($block_type) && $block_type == 'Check_Fix_allparticle') echo 'selected'; ?>>---------Check ---Fix_all.particle---------</option>
					<option value="Fix_allparticle" <?php if (isset($block_type) && $block_type == 'Fix_allparticle') echo 'selected'; ?>>Fix_all.particle</option>
					<option value="Check_Fix_alleffect" <?php if (isset($block_type) && $block_type == 'Check_Fix_alleffect') echo 'selected'; ?>>---------Check ---Fix_all.effect---------</option>
					<option value="Fix_alleffect" <?php if (isset($block_type) && $block_type == 'Fix_alleffect') echo 'selected'; ?>>Fix_all.effect</option>
					<option value="Check_Fix_allskill" <?php if (isset($block_type) && $block_type == 'Check_Fix_allskill') echo 'selected'; ?>>---------Check ---Fix_all.skill---------</option>
					<option value="Fix_allskill" <?php if (isset($block_type) && $block_type == 'Fix_allskill') echo 'selected'; ?>>Fix_all.skill</option>
					<option value="Fix_STT" <?php if (isset($block_type) && $block_type == 'Fix_STT') echo 'selected'; ?>>Fix_STT</option>
					<option value="StrDictionary" <?php if (isset($block_type) && $block_type == 'StrDictionary') echo 'selected'; ?>>Merge ****** StrDictionary</option>
					<option value="particle" <?php if (isset($block_type) && $block_type == 'particle') echo 'selected'; ?>>Merge ****** Particle</option>
					<option value="skill" <?php if (isset($block_type) && $block_type == 'skill') echo 'selected'; ?>>Merge ****** Skill</option>
					<option value="material" <?php if (isset($block_type) && $block_type == 'material') echo 'selected'; ?>>Merge ****** Material</option>
					<option value="effect" <?php if (isset($block_type) && $block_type == 'effect') echo 'selected'; ?>>Merge ****** Effect</option>
					<option value="obj" <?php if (isset($block_type) && $block_type == 'obj') echo 'selected'; ?>>Merge ****** Object</option>
					<option value="Fix_loi_khoi_particle" <?php if (isset($block_type) && $block_type == 'Fix_loi_khoi_particle') echo 'selected'; ?>>==========Fix lỗi khối File==========</option>
                </select>
            </div>
            <div class="form-group">
                <label for="file1">Nhập vào File 1:</label>
                <input type="file" class="form-control-file" name="file1" id="file1">
                <?php if (isset($file1_name)) : ?>
                    <small class="form-text text-muted">Tên file 1: <?php echo $file1_name; ?> (mặc định không nhập đường dẫn hiện tại \merged_( loại ).txt)</small>
                <?php endif; ?>
            </div>
            <div class="form-group">
                <label for="file2">Nhập vào File 2:</label>
                <input type="file" class="form-control-file" name="file2" id="file2">
                <?php if (isset($file2_name)) : ?>
                    <small class="form-text text-muted">Tên file 2: <?php echo $file2_name; ?></small>
                <?php endif; ?>
            </div>
				<input type="submit" class="btn btn-primary submit-button" name="MergeFiles" value="Hợp Nhất(Merge ****** ) Particle - Skill - Material - Effect - Object"><br>
				<input type="submit" class="btn btn-primary submit-button" name="StrDictionary" value="Hợp Nhất StrDictionary"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Check_Fix_mesh_skeleton_IMG" value="--------------Check Fix mesh_skeleton_IMG"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Check_Fix_allmaterial" value="--------------Check Fix allmaterial"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Extract_allmaterial" value="Extract_allmaterial"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Check_Fix_allparticle" value="--------------Check Fix all.particle"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Fix_allparticle" value="Fix all.particle"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Check_Fix_alleffect" value="--------------Check Fix all.effect"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Fix_alleffect" value="Fix all.effect"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Check_Fix_allskill" value="--------------Check Fix all.skill"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Fix_allskill" value="Fix all.skill"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Fix_STT" value="Fix Thứ tự dòng"><br>
				<input type="submit" class="btn btn-primary submit-button" name="Fix_loi_khoi_particle" value="==========Fix lỗi khối File=========="><br>
        </form>
        <br>
        <?php if (!empty($log_content)): ?>
            <div class="alert alert-info log-content" role="alert">
                <?php echo nl2br($log_content); ?>
            </div>
        <?php endif; ?>
    </div>
</body>
</html>
