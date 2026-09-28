<?php
/**
 * PDO database connection for Irrigation Gateway
 * Place this file at irrigation_gateway/api/db.php
 * Copy to irrigation_gateway/db.php for legacy scripts if needed
 */

// --- Configuration (adjust for your environment) ---
$DB_HOST = 'localhost';
$DB_NAME = 'irrigation';
$DB_USER = 'irrigation_user';
$DB_PASS = 'strong_password_here';  // CHANGE THIS
$DB_CHARSET = 'utf8mb4';
// ----------------------------------------------------

$dsn = "mysql:host={$DB_HOST};dbname={$DB_NAME};charset={$DB_CHARSET}";

$options = [
    PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    PDO::ATTR_EMULATE_PREPARES   => false,  // Use native prepared statements
    PDO::MYSQL_ATTR_INIT_COMMAND => "SET NAMES {$DB_CHARSET} COLLATE {$DB_CHARSET}_unicode_ci",
];

try {
    $pdo = new PDO($dsn, $DB_USER, $DB_PASS, $options);
} catch (PDOException $e) {
    // In production, log this instead of echoing
    error_log("DB connection failed: " . $e->getMessage());
    http_response_code(503);
    header('Content-Type: application/json');
    echo json_encode(['ok' => false, 'error' => 'Database unavailable']);
    exit;
}

// Helper: execute a SELECT and return all rows
function db_select(PDO $pdo, string $sql, array $params = []): array {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return $stmt->fetchAll();
}

// Helper: execute a SELECT and return one row
function db_select_one(PDO $pdo, string $sql, array $params = []): ?array {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $row = $stmt->fetch();
    return $row ?: null;
}

// Helper: execute INSERT/UPDATE/DELETE, return affected rows
function db_exec(PDO $pdo, string $sql, array $params = []): int {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return $stmt->rowCount();
}

// Helper: execute INSERT and return last insert ID
function db_insert(PDO $pdo, string $sql, array $params = []): string {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return $pdo->lastInsertId();
}