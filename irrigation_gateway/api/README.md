# Irrigation Gateway API

## Overview
REST API layer between Webpage ↔ MySQL ↔ Gateway (C). All validation/sanitization happens here.

## Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/config` | Get all config (with metadata) |
| POST | `/api/config` | Update config (validated) |
| POST | `/api/telemetry` | Gateway stores reading |
| POST | `/api/command` | Webpage queues command |
| GET | `/api/command/next` | Gateway fetches next pending |
| POST | `/api/command/ack` | Gateway reports ACK/failed |
| GET | `/api/readings/latest?node=ID` | Latest reading for node |
| GET | `/api/readings?node=ID&from=&to=&limit=` | Historical data |
| GET | `/api/readings/nodes` | List all nodes with last seen |

## Deployment

### 1. Database
```bash
mysql -u root -p < ../sql/schema.sql
```

### 2. Configure DB credentials
Edit `db.php`:
```php
$DB_HOST = 'localhost';
$DB_NAME = 'irrigation';
$DB_USER = 'irrigation_user';
$DB_PASS = 'your_strong_password';
```

### 3. Web Server (Apache)
```apache
<VirtualHost *:80>
    DocumentRoot /path/to/irrigation_gateway/api
    <Directory /path/to/irrigation_gateway/api>
        Options -Indexes +FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>
</VirtualHost>
```
Enable mod_rewrite: `a2enmod rewrite && systemctl reload apache2`

### 4. Web Server (Nginx)
```nginx
server {
    listen 80;
    root /path/to/irrigation_gateway/api;
    index index.php;

    location / {
        try_files $uri $uri/ /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass unix:/run/php/php8.2-fpm.sock;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }
}
```

## Config Validation Rules

| Key | Type | Min | Max | Cross-check |
|-----|------|-----|-----|-------------|
| thresh_low | int | 0 | 95 | < thresh_high |
| thresh_high | int | 5 | 100 | > thresh_low |
| rain_aware_enabled | bool | - | - | - |
| rain_forecast_hours | int | 1 | 24 | - |
| rain_forecast_mm | int | 0 | 50 | - |
| rain_hold_cap | int | 1 | 99 | between low/high |
| rain_humidity_skip | int | 50 | 95 | - |
| rain_emergency_floor | int | 0 | 50 | - |
| auto_resume_enabled | bool | - | - | - |
| auto_resume_readings | int | 1 | 100 | - |

## Example Requests

### Get all config
```bash
curl http://localhost/api/config
```

### Update thresholds
```bash
curl -X POST http://localhost/api/config \
  -H "Content-Type: application/json" \
  -d '{"thresh_low": 18, "thresh_high": 32}'
```

### Gateway posts telemetry (includes auto_mode)
```bash
curl -X POST http://localhost/api/telemetry \
  -H "Content-Type: application/json" \
  -d '{"datetime":1727452800,"id_sn":12345678901234,"id_gw":998877665544,
       "soil_pct":42,"temp_c":25.3,"hum_pct":68,"pump_state":true,
       "rain_detected":false,"sensor_fault":false,"active_count":3,
       "probe_map":0,"water_left":0,"flags":2,"auto_mode":1}'
```

### Webpage queues command
```bash
curl -X POST http://localhost/api/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"THRESH:18,32"}'
```

### Gateway fetches next command
```bash
curl http://localhost/api/command/next
```

### Gateway acknowledges
```bash
curl -X POST http://localhost/api/command/ack \
  -H "Content-Type: application/json" \
  -d '{"id":123,"status":"acked"}'
```

### Fetch latest reading
```bash
curl "http://localhost/api/readings/latest?node=12345678901234"
```

### Fetch history for charts
```bash
curl "http://localhost/api/readings?node=12345678901234&from=1727400000&to=1727486400&limit=200"
```

## Security Notes

1. **Change default DB password** in `db.php`
2. **Restrict API access** - add IP whitelist or auth in production:
   ```php
   // In index.php, before routing:
   $allowed = ['192.168.1.0/24', '10.0.0.0/8'];
   $client_ip = $_SERVER['REMOTE_ADDR'];
   if (!ip_in_ranges($client_ip, $allowed)) { http_response_code(403); exit; }
   ```
3. **Use HTTPS** in production (Let's Encrypt)
4. **Rate limit** command endpoint to prevent abuse

## Gateway Integration

The Gateway C code should:
1. **Config poller (30s)**: `GET /api/config` → diff → `BLE_Send_Thresh()` etc.
2. **Telemetry poster**: `POST /api/telemetry` on each frame received
3. **Command fetcher (5s)**: `GET /api/command/next` → `BLE_Send_Command()` → `POST /api/command/ack`
4. **Forecast fetcher (1h)**: Open-Meteo → `POST /api/command` with `RAINF:mm,hours`

See `../http_client.c` for implementation.