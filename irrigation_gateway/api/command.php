<?php
/**
 * Command API - Webpage queues commands, Gateway polls and forwards via BLE
 * POST /api/command           -> queue a command (validated)
 * GET  /api/command/next      -> Gateway fetches next pending command
 * POST /api/command/ack       -> Gateway reports ACK/NACK
 */

require_once __DIR__ . '/db.php';

header('Content-Type: application/json');

// Allowed command patterns
$ALLOWED_EXACT = ['PUMP:ON', 'PUMP:OFF', 'AUTO:ON', 'AUTO:OFF', 'RAIN:ON', 'RAIN:OFF', 'STATUS'];

function validate_command(string $cmd): array {
    global $ALLOWED_EXACT;

    $cmd = trim($cmd);
    if ($cmd === '') return ['ok' => false, 'error' => 'Empty command'];

    // Exact matches
    if (in_array($cmd, $ALLOWED_EXACT, true)) {
        return ['ok' => true];
    }

    // THRESH:low,high
    if (strpos($cmd, 'THRESH:') === 0) {
        $parts = explode(',', substr($cmd, 7), 2);
        if (count($parts) !== 2) return ['ok' => false, 'error' => 'THRESH requires low,high'];
        if (!ctype_digit($parts[0]) || !ctype_digit($parts[1])) return ['ok' => false, 'error' => 'THRESH values must be integers'];
        $low = (int)$parts[0];
        $high = (int)$parts[1];
        if ($low < 0 || $low > 95) return ['ok' => false, 'error' => 'thresh_low 0-95'];
        if ($high < 5 || $high > 100) return ['ok' => false, 'error' => 'thresh_high 5-100'];
        if ($low >= $high) return ['ok' => false, 'error' => 'thresh_low must be < thresh_high'];
        return ['ok' => true];
    }

    // RAINF:mm,hours
    if (strpos($cmd, 'RAINF:') === 0) {
        $parts = explode(',', substr($cmd, 6), 2);
        if (count($parts) !== 2) return ['ok' => false, 'error' => 'RAINF requires mm,hours'];
        if (!ctype_digit($parts[0]) || !ctype_digit($parts[1])) return ['ok' => false, 'error' => 'RAINF values must be integers'];
        $mm = (int)$parts[0];
        $hours = (int)$parts[1];
        if ($mm < 0 || $mm > 500) return ['ok' => false, 'error' => 'rain mm 0-500'];
        if ($hours < 1 || $hours > 24) return ['ok' => false, 'error' => 'rain hours 1-24'];
        return ['ok' => true];
    }

    // WATER:secs
    if (strpos($cmd, 'WATER:') === 0) {
        $secs = (int)substr($cmd, 6);
        if ($secs < 1 || $secs > 300000) return ['ok' => false, 'error' => 'WATER secs 1-300000'];
        return ['ok' => true];
    }

    return ['ok' => false, 'error' => 'Invalid command format'];
}

// POST /api/command - queue a command
if ($_SERVER['REQUEST_METHOD'] === 'POST' && $_SERVER['REQUEST_URI'] === '/api/command') {
    $input = json_decode(file_get_contents('php://input'), true);
    if (!is_array($input) || !isset($input['cmd'])) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'Missing cmd in JSON body']);
        exit;
    }

    $cmd = (string)$input['cmd'];
    $result = validate_command($cmd);
    if (!$result['ok']) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => $result['error']]);
        exit;
    }

    try {
        $id = db_insert($pdo,
            "INSERT INTO command_queue (cmd, created_at, status) VALUES (?, UNIX_TIMESTAMP(), 'pending')",
            [$cmd]
        );
        echo json_encode(['ok' => true, 'id' => (int)$id, 'cmd' => $cmd]);
    } catch (Exception $e) {
        error_log("Command queue INSERT failed: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'Database error']);
    }
    exit;
}

// GET /api/command/next - Gateway fetches oldest pending command
if ($_SERVER['REQUEST_METHOD'] === 'GET' && strpos($_SERVER['REQUEST_URI'], '/api/command/next') === 0) {
    try {
        $row = db_select_one($pdo,
            "SELECT id, cmd FROM command_queue WHERE status = 'pending' ORDER BY created_at ASC LIMIT 1"
        );
        if (!$row) {
            echo json_encode(['ok' => true, 'cmd' => null]);
            exit;
        }
        // Mark as sent
        db_exec($pdo, "UPDATE command_queue SET status = 'sent', sent_at = UNIX_TIMESTAMP() WHERE id = ?", [$row['id']]);
        echo json_encode(['ok' => true, 'id' => (int)$row['id'], 'cmd' => $row['cmd']]);
    } catch (Exception $e) {
        error_log("Command fetch failed: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'Database error']);
    }
    exit;
}

// POST /api/command/ack - Gateway reports result
if ($_SERVER['REQUEST_METHOD'] === 'POST' && strpos($_SERVER['REQUEST_URI'], '/api/command/ack') === 0) {
    $input = json_decode(file_get_contents('php://input'), true);
    if (!is_array($input) || !isset($input['id'], $input['status'])) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'Missing id or status']);
        exit;
    }

    $id = (int)$input['id'];
    $status = $input['status'];
    if (!in_array($status, ['acked', 'failed'], true)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'Invalid status']);
        exit;
    }

    try {
        $field = $status === 'acked' ? 'ack_at' : 'sent_at'; // failed -> clear sent_at for retry?
        db_exec($pdo,
            "UPDATE command_queue SET status = ?, $field = UNIX_TIMESTAMP() WHERE id = ?",
            [$status, $id]
        );
        echo json_encode(['ok' => true]);
    } catch (Exception $e) {
        error_log("Command ack failed: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'Database error']);
    }
    exit;
}

http_response_code(404);
echo json_encode(['ok' => false, 'error' => 'Not found']);