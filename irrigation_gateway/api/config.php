<?php
/**
 * Config API - Single source of truth for runtime configuration
 * GET  /api/config              -> returns all config
 * POST /api/config              -> updates one or more keys (validated)
 * 
 * Validation rules enforced here:
 * - Type checking (int, float, bool, string)
 * - Range checking (min/max from DB)
 * - Cross-field (thresh_low < thresh_high)
 * - Prepared statements (SQL injection safe)
 */

require_once __DIR__ . '/db.php';

header('Content-Type: application/json');
header('Cache-Control: no-store, no-cache, must-revalidate');

// Load validation rules from DB config table (with hardcoded fallbacks)
function load_validation_rules(PDO $pdo): array {
    $defaults = [
        'thresh_low'          => ['type' => 'int',    'min' => 0,   'max' => 95],
        'thresh_high'         => ['type' => 'int',    'min' => 5,   'max' => 100],
        'rain_aware_enabled'  => ['type' => 'bool',   'min' => null,'max' => null],
        'rain_forecast_hours' => ['type' => 'int',    'min' => 1,   'max' => 24],
        'rain_forecast_mm'    => ['type' => 'int',    'min' => 0,   'max' => 50],
        'rain_hold_cap'       => ['type' => 'int',    'min' => 1,   'max' => 99],
        'rain_humidity_skip'  => ['type' => 'int',    'min' => 50,  'max' => 95],
        'rain_emergency_floor'=> ['type' => 'int',    'min' => 0,   'max' => 50],
        'auto_resume_enabled' => ['type' => 'bool',   'min' => null,'max' => null],
        'auto_resume_readings'=> ['type' => 'int',    'min' => 1,   'max' => 100],
    ];

    try {
        $rows = db_select($pdo, "SELECT key_name, type, min_val, max_val FROM config");
        foreach ($rows as $r) {
            $key = $r['key_name'];
            if (isset($defaults[$key])) {
                $defaults[$key]['type'] = $r['type'];
                $defaults[$key]['min']  = $r['min_val'] !== '' && $r['min_val'] !== null ? (float)$r['min_val'] : null;
                $defaults[$key]['max']  = $r['max_val'] !== '' && $r['max_val'] !== null ? (float)$r['max_val'] : null;
            }
        }
    } catch (Exception $e) {
        error_log("Config rules load failed: " . $e->getMessage());
    }
    return $defaults;
}

// Validate and sanitize a single value
function validate_value(string $key, $value, array $rule): array {
    $type = $rule['type'];
    $min  = $rule['min'];
    $max  = $rule['max'];

    // Type coercion + validation
    switch ($type) {
        case 'int':
            if (!is_scalar($value) || (is_string($value) && !ctype_digit($value)) || (is_float($value) && floor($value) != $value)) {
                return ['ok' => false, 'error' => "$key must be integer"];
            }
            $value = (int)$value;
            break;
        case 'float':
            if (!is_numeric($value)) {
                return ['ok' => false, 'error' => "$key must be numeric"];
            }
            $value = (float)$value;
            break;
        case 'bool':
            if (is_bool($value)) {
                $value = $value ? '1' : '0';
            } elseif (is_string($value)) {
                $v = strtolower($value);
                if (in_array($v, ['1','true','yes','on'])) $value = '1';
                elseif (in_array($v, ['0','false','no','off'])) $value = '0';
                else return ['ok' => false, 'error' => "$key must be true/false"];
            } else {
                return ['ok' => false, 'error' => "$key must be boolean"];
            }
            break;
        case 'string':
            $value = trim((string)$value);
            if (strlen($value) > 256) {
                return ['ok' => false, 'error' => "$key too long (max 256)"];
            }
            break;
        default:
            return ['ok' => false, 'error' => "Unknown type for $key"];
    }

    // Range check
    if ($min !== null && $value < $min) return ['ok' => false, 'error' => "$key below minimum ($min)"];
    if ($max !== null && $value > $max) return ['ok' => false, 'error' => "$key above maximum ($max)"];

    return ['ok' => true, 'value' => $value];
}

// Cross-field validation
function cross_validate(array $input, array $validated): array {
    // thresh_low < thresh_high
    if (isset($validated['thresh_low']) && isset($input['thresh_high'])) {
        if ($validated['thresh_low'] >= (int)$input['thresh_high']) {
            return ['ok' => false, 'error' => 'thresh_low must be < thresh_high'];
        }
    }
    if (isset($validated['thresh_high']) && isset($input['thresh_low'])) {
        if ($validated['thresh_high'] <= (int)$input['thresh_low']) {
            return ['ok' => false, 'error' => 'thresh_high must be > thresh_low'];
        }
    }
    // rain_hold_cap between thresh_low and thresh_high
    if (isset($validated['rain_hold_cap'])) {
        $low  = $validated['thresh_low']  ?? ($input['thresh_low']  ?? null);
        $high = $validated['thresh_high'] ?? ($input['thresh_high'] ?? null);
        if ($low !== null && $validated['rain_hold_cap'] <= (int)$low) {
            return ['ok' => false, 'error' => 'rain_hold_cap must be > thresh_low'];
        }
        if ($high !== null && $validated['rain_hold_cap'] >= (int)$high) {
            return ['ok' => false, 'error' => 'rain_hold_cap must be < thresh_high'];
        }
    }
    return ['ok' => true];
}

// GET /api/config
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    try {
        $rows = db_select($pdo, "SELECT key_name, value, type, min_val, max_val, description, updated_at FROM config ORDER BY key_name");
        $config = [];
        foreach ($rows as $r) {
            $config[$r['key_name']] = [
                'value'      => $r['value'],
                'type'       => $r['type'],
                'min_val'    => $r['min_val'],
                'max_val'    => $r['max_val'],
                'description'=> $r['description'],
                'updated_at' => $r['updated_at'],
            ];
        }
        echo json_encode(['ok' => true, 'config' => $config]);
    } catch (Exception $e) {
        error_log("Config GET failed: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'Database error']);
    }
    exit;
}

// POST /api/config
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $input = json_decode(file_get_contents('php://input'), true);
    if (!is_array($input)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'Invalid JSON body']);
        exit;
    }

    $rules = load_validation_rules($pdo);
    $errors = [];
    $validated = [];

    foreach ($input as $key => $value) {
        if (!isset($rules[$key])) {
            $errors[$key] = "Unknown config key";
            continue;
        }
        $result = validate_value($key, $value, $rules[$key]);
        if (!$result['ok']) {
            $errors[$key] = $result['error'];
        } else {
            $validated[$key] = $result['value'];
        }
    }

    // Cross-field validation
    $cross = cross_validate($input, $validated);
    if (!$cross['ok']) {
        // Find which field caused it (approximate)
        $errors['cross'] = $cross['error'];
    }

    if (!empty($errors)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'errors' => $errors]);
        exit;
    }

    // Transaction: all or nothing
    try {
        $pdo->beginTransaction();
        foreach ($validated as $key => $value) {
            db_exec($pdo, "UPDATE config SET value = ? WHERE key_name = ?", [$value, $key]);
        }
        $pdo->commit();
        echo json_encode(['ok' => true, 'updated' => array_keys($validated)]);
    } catch (Exception $e) {
        $pdo->rollBack();
        error_log("Config POST transaction failed: " . $e->getMessage());
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'Database error']);
    }
    exit;
}

http_response_code(405);
echo json_encode(['ok' => false, 'error' => 'Method not allowed']);