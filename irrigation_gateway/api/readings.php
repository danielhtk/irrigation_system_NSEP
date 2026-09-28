<?php
/**
 * Readings API - Historical telemetry for dashboard charts
 * GET /api/readings?node=ID_SN&from=UNIX&to=UNIX&limit=N
 * GET /api/readings/latest?node=ID_SN
 */

require_once __DIR__ . '/db.php';

header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'Method not allowed']);
    exit;
}

$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$query = $_GET;

try {
    // GET /api/readings/latest?node=12345
    if (strpos($path, '/api/readings/latest') === 0) {
        $id_sn = isset($query['node']) ? (int)$query['node'] : null;
        if (!$id_sn) {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'Missing node parameter']);
            exit;
        }

        $row = db_select_one($pdo,
            "SELECT * FROM readings WHERE id_sn = ? ORDER BY datetime DESC LIMIT 1",
            [$id_sn]
        );
        if (!$row) {
            echo json_encode(['ok' => true, 'reading' => null]);
            exit;
        }
        // Convert types for JSON
        $row['pump_state'] = (bool)$row['pump_state'];
        $row['rain_detected'] = (bool)$row['rain_detected'];
        $row['sensor_fault'] = (bool)$row['sensor_fault'];
        $row['auto_mode'] = $row['auto_mode'] !== null ? (bool)$row['auto_mode'] : null;
        echo json_encode(['ok' => true, 'reading' => $row]);
        exit;
    }

    // GET /api/readings?node=12345&from=1727400000&to=1727486400&limit=100
    if ($path === '/api/readings') {
        $id_sn = isset($query['node']) ? (int)$query['node'] : null;
        $from  = isset($query['from']) ? (int)$query['from'] : 0;
        $to    = isset($query['to']) ? (int)$query['to'] : time();
        $limit = isset($query['limit']) ? min((int)$query['limit'], 1000) : 100;

        if (!$id_sn) {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'Missing node parameter']);
            exit;
        }

        $sql = "SELECT * FROM readings WHERE id_sn = ? AND datetime BETWEEN ? AND ? ORDER BY datetime ASC LIMIT ?";
        $rows = db_select($pdo, $sql, [$id_sn, $from, $to, $limit]);

        // Convert boolean fields
        foreach ($rows as &$r) {
            $r['pump_state'] = (bool)$r['pump_state'];
            $r['rain_detected'] = (bool)$r['rain_detected'];
            $r['sensor_fault'] = (bool)$r['sensor_fault'];
            $r['auto_mode'] = $r['auto_mode'] !== null ? (bool)$r['auto_mode'] : null;
        }

        echo json_encode(['ok' => true, 'count' => count($rows), 'readings' => $rows]);
        exit;
    }

    // GET /api/readings/nodes - list all node IDs with latest timestamp
    if ($path === '/api/readings/nodes') {
        $rows = db_select($pdo,
            "SELECT id_sn, MAX(datetime) as last_seen, COUNT(*) as count
             FROM readings GROUP BY id_sn ORDER BY last_seen DESC"
        );
        echo json_encode(['ok' => true, 'nodes' => $rows]);
        exit;
    }

    http_response_code(404);
    echo json_encode(['ok' => false, 'error' => 'Not found']);

} catch (Exception $e) {
    error_log("Readings API error: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['ok' => false, 'error' => 'Database error']);
}