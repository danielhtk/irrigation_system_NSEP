<?php
/**
 * Telemetry API - Called by Gateway C code to store readings
 * POST /api/telemetry
 * 
 * Expected JSON body:
 * {
 *   "datetime": 1727452800,
 *   "id_sn": 12345678901234,
 *   "id_gw": 998877665544,
 *   "soil_pct": 42,
 *   "temp_c": 25.3,
 *   "hum_pct": 68,
 *   "pump_state": true,
 *   "rain_detected": false,
 *   "sensor_fault": false,
 *   "active_count": 3,
 *   "probe_map": 0,
 *   "water_left": 0,
 *   "flags": 2,
 *   "auto_mode": 1          // NEW: 1=auto, 0=manual (optional for backward compat)
 * }
 * 
 * All fields required except temp_c, hum_pct, auto_mode (nullable)
 */

require_once __DIR__ . '/db.php';

header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'Method not allowed']);
    exit;
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Invalid JSON body']);
    exit;
}

// Required fields (auto_mode is optional for backward compatibility)
$required = [
    'datetime', 'id_sn', 'id_gw', 'soil_pct',
    'pump_state', 'rain_detected', 'sensor_fault',
    'active_count', 'probe_map', 'water_left', 'flags'
];

foreach ($required as $f) {
    if (!array_key_exists($f, $input)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => "Missing field: $f"]);
        exit;
    }
}

// Type coercion + basic validation
$datetime      = (int)$input['datetime'];
$id_sn         = (int)$input['id_sn'];
$id_gw         = (int)$input['id_gw'];
$soil_pct      = (int)$input['soil_pct'];
$temp_c        = array_key_exists('temp_c', $input) && $input['temp_c'] !== null ? (float)$input['temp_c'] : null;
$hum_pct       = array_key_exists('hum_pct', $input) && $input['hum_pct'] !== null ? (int)$input['hum_pct'] : null;
$pump_state    = (bool)$input['pump_state'];
$rain_detected = (bool)$input['rain_detected'];
$sensor_fault  = (bool)$input['sensor_fault'];
$active_count  = (int)$input['active_count'];
$probe_map     = (int)$input['probe_map'];
$water_left    = (int)$input['water_left'];
$flags         = (int)$input['flags'];
$auto_mode     = array_key_exists('auto_mode', $input) ? (int)$input['auto_mode'] : null;

// Range checks
if ($datetime <= 0 || $datetime > time() + 3600) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'Invalid datetime']); exit; }
if ($soil_pct < 0 || $soil_pct > 100)                 { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'soil_pct 0-100']); exit; }
if ($temp_c !== null && ($temp_c < -40 || $temp_c > 80)) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'temp_c out of range']); exit; }
if ($hum_pct !== null && ($hum_pct < 0 || $hum_pct > 100)) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'hum_pct 0-100']); exit; }
if ($active_count < 0 || $active_count > 8)           { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'active_count 0-8']); exit; }
if ($probe_map < 0 || $probe_map > 255)               { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'probe_map 0-255']); exit; }
if ($water_left < 0 || $water_left > 300000)          { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'water_left out of range']); exit; }
if ($flags < 0 || $flags > 255)                       { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'flags 0-255']); exit; }
if ($auto_mode !== null && ($auto_mode < 0 || $auto_mode > 1)) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'auto_mode 0-1']); exit; }

try {
    $sql = "INSERT INTO readings
        (datetime, id_sn, id_gw, soil_pct, temp_c, hum_pct,
         pump_state, rain_detected, sensor_fault,
         active_count, probe_map, water_left, flags, auto_mode)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    $stmt = $pdo->prepare($sql);
    $stmt->execute([
        $datetime, $id_sn, $id_gw, $soil_pct, $temp_c, $hum_pct,
        $pump_state, $rain_detected, $sensor_fault,
        $active_count, $probe_map, $water_left, $flags, $auto_mode
    ]);
    echo json_encode(['ok' => true, 'id' => (int)$pdo->lastInsertId()]);
} catch (PDOException $e) {
    error_log("Telemetry INSERT failed: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['ok' => false, 'error' => 'Database error']);
}