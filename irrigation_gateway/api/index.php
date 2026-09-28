<?php
/**
 * Front controller for Irrigation Gateway API
 * Place at irrigation_gateway/api/index.php
 * Requires Apache with mod_rewrite or Nginx config to route all requests here
 */

// Enable CORS for local development (adjust origin in production)
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// Handle preflight
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Route to appropriate handler
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);

switch (true) {
    case $path === '/api/config' || $path === '/api/config/':
        require_once __DIR__ . '/config.php';
        break;
    case $path === '/api/telemetry':
        require_once __DIR__ . '/telemetry.php';
        break;
    case strpos($path, '/api/command') === 0:
        require_once __DIR__ . '/command.php';
        break;
    case strpos($path, '/api/readings') === 0:
        require_once __DIR__ . '/readings.php';
        break;
    default:
        http_response_code(404);
        header('Content-Type: application/json');
        echo json_encode(['ok' => false, 'error' => 'API endpoint not found', 'path' => $path]);
}