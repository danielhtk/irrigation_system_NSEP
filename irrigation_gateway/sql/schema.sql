-- Irrigation Gateway Database Schema
-- Run: mysql -u root -p < schema.sql
-- Or: mysql -u irrigation_user -p irrigation < schema.sql

CREATE DATABASE IF NOT EXISTS `irrigation` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `irrigation`;

-- ============================================================
-- readings: Telemetry from Arduino (via Gateway)
-- One row per DATA/telemetry frame received
-- ============================================================
CREATE TABLE IF NOT EXISTS `readings` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `datetime` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp (seconds) from gateway',
    `id_sn` BIGINT UNSIGNED NOT NULL COMMENT 'Node serial (48-bit BLE MAC as integer)',
    `id_gw` BIGINT UNSIGNED NOT NULL COMMENT 'Gateway ID',
    `soil_pct` TINYINT UNSIGNED NOT NULL COMMENT 'Voted soil moisture 0-100%',
    `temp_c` DECIMAL(4,1) DEFAULT NULL COMMENT 'Temperature Celsius, one decimal',
    `hum_pct` TINYINT UNSIGNED DEFAULT NULL COMMENT 'Humidity 0-100%',
    `pump_state` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '1=pump running',
    `rain_detected` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '1=rain (manual or auto)',
    `sensor_fault` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '1=sensor voting failed',
    `active_count` TINYINT UNSIGNED NOT NULL COMMENT 'Probes that voted (2-3)',
    `probe_map` TINYINT UNSIGNED NOT NULL COMMENT 'Bitmask: 1=probe excluded',
    `water_left` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Seconds left on timed run',
    `flags` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Bit 0=rain, 1=pump, 2=fault',
    `auto_mode` TINYINT UNSIGNED DEFAULT NULL COMMENT '1=auto, 0=manual, NULL=unknown',
    INDEX `idx_datetime` (`datetime`),
    INDEX `idx_id_sn` (`id_sn`),
    INDEX `idx_id_gw_datetime` (`id_gw`, `datetime`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- config: Runtime configuration (single source of truth)
-- Webpage writes here (validated); Gateway polls and pushes to Arduino
-- ============================================================
CREATE TABLE IF NOT EXISTS `config` (
    `key_name` VARCHAR(64) NOT NULL PRIMARY KEY,
    `value` VARCHAR(256) NOT NULL,
    `type` ENUM('int','float','bool','string') NOT NULL,
    `min_val` VARCHAR(64) DEFAULT NULL,
    `max_val` VARCHAR(64) DEFAULT NULL,
    `description` TEXT,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Default configuration (matches firmware default_config.h)
INSERT IGNORE INTO `config` (`key_name`, `value`, `type`, `min_val`, `max_val`, `description`) VALUES
('thresh_low',          '20',  'int',  '0',   '95',  'Pump ON threshold (%)'),
('thresh_high',         '30',  'int',  '5',   '100', 'Pump OFF threshold (%)'),
('rain_aware_enabled',  '1',   'bool', NULL,  NULL,  'Enable rain-aware logic (1=on)'),
('rain_forecast_hours', '12',  'int',  '1',   '24',  'Forecast lookahead (hours)'),
('rain_forecast_mm',    '2',   'int',  '0',   '50',  'Min rain (mm) to trigger'),
('rain_hold_cap',       '25',  'int',  '1',   '99',  'Hold cap Z (%) between low/high'),
('rain_humidity_skip',  '80',  'int',  '50',  '95',  'Humidity skip threshold (%)'),
('rain_emergency_floor','15',  'int',  '0',   '50',  'Emergency floor (%) - ignore rain below this'),
('auto_resume_enabled', '0',   'bool', NULL,  NULL,  'Auto-resume after fault (1=on)'),
('auto_resume_readings','10',  'int',  '1',   '100', 'Healthy readings before auto-resume');

-- ============================================================
-- command_queue: Commands from webpage → Gateway → Arduino
-- Gateway polls this table and forwards via BLE
-- ============================================================
CREATE TABLE IF NOT EXISTS `command_queue` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `cmd` VARCHAR(128) NOT NULL COMMENT 'Full command string, e.g. THRESH:20,30',
    `created_at` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp',
    `sent_at` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Unix timestamp when sent to Arduino',
    `ack_at` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Unix timestamp when ACK received',
    `status` ENUM('pending','sent','acked','failed') NOT NULL DEFAULT 'pending',
    INDEX `idx_status_created` (`status`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- users: Optional - for webpage authentication
-- ============================================================
CREATE TABLE IF NOT EXISTS `users` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `username` VARCHAR(64) NOT NULL UNIQUE,
    `password_hash` VARCHAR(255) NOT NULL COMMENT 'bcrypt/argon2',
    `role` ENUM('admin','viewer') NOT NULL DEFAULT 'viewer',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `last_login` TIMESTAMP NULL DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- View: Latest reading per node (for dashboard status)
-- ============================================================
CREATE OR REPLACE VIEW `latest_readings` AS
SELECT r1.*
FROM `readings` r1
JOIN (
    SELECT `id_sn`, MAX(`datetime`) AS max_dt
    FROM `readings`
    GROUP BY `id_sn`
) r2 ON r1.`id_sn` = r2.`id_sn` AND r1.`datetime` = r2.`max_dt`;

-- ============================================================
-- Migration: Add auto_mode column to existing readings table
-- Run if upgrading from schema without auto_mode
-- ============================================================
-- ALTER TABLE `readings` ADD COLUMN `auto_mode` TINYINT UNSIGNED DEFAULT NULL COMMENT '1=auto, 0=manual, NULL=unknown' AFTER `flags`;

-- ============================================================
-- Grants (adjust user/host as needed)
-- ============================================================
-- CREATE USER IF NOT EXISTS 'irrigation_user'@'%' IDENTIFIED BY 'strong_password_here';
-- GRANT SELECT, INSERT, UPDATE ON `irrigation`.`readings` TO 'irrigation_user'@'%';
-- GRANT SELECT, UPDATE ON `irrigation`.`config` TO 'irrigation_user'@'%';
-- GRANT SELECT, INSERT, UPDATE ON `irrigation`.`command_queue` TO 'irrigation_user'@'%';
-- FLUSH PRIVILEGES;