#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// Current status
static motor_control_t current_motor_data = {0};
static remote_settings_t current_settings = {0};
static uint16_t adc_x = 2048, adc_y = 2048, adc_x2 = 2048, adc_y2 = 2048;

/**
 * HTML/CSS/JavaScript for web interface
 */
static const char *html_content = ""
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"    <meta charset=\"UTF-8\">"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"    <title>Remote Transmitter Control</title>"
"    <style>"
"        * { margin: 0; padding: 0; box-sizing: border-box; }"
"        body {"
"            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"            min-height: 100vh;"
"            padding: 20px;"
"        }"
"        .container {"
"            max-width: 1200px;"
"            margin: 0 auto;"
"            background: white;"
"            border-radius: 15px;"
"            box-shadow: 0 20px 60px rgba(0,0,0,0.3);"
"            overflow: hidden;"
"        }"
"        .header {"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"            color: white;"
"            padding: 30px;"
"            text-align: center;"
"        }"
"        .header h1 { font-size: 28px; margin-bottom: 10px; }"
"        .header p { font-size: 14px; opacity: 0.9; }"
"        .content {"
"            display: grid;"
"            grid-template-columns: 1fr 1fr;"
"            gap: 30px;"
"            padding: 30px;"
"        }"
"        .card {"
"            background: #f8f9fa;"
"            border-radius: 10px;"
"            padding: 20px;"
"            border: 1px solid #e0e0e0;"
"        }"
"        .card h2 {"
"            color: #333;"
"            font-size: 18px;"
"            margin-bottom: 20px;"
"            border-bottom: 3px solid #667eea;"
"            padding-bottom: 10px;"
"        }"
"        .status-group {"
"            margin-bottom: 15px;"
"        }"
"        .status-label {"
"            font-size: 13px;"
"            color: #666;"
"            font-weight: 600;"
"            text-transform: uppercase;"
"            letter-spacing: 0.5px;"
"        }"
"        .status-value {"
"            font-size: 16px;"
"            color: #333;"
"            margin-top: 5px;"
"            font-weight: 500;"
"        }"
"        .joystick-container {"
"            position: relative;"
"            width: 150px;"
"            height: 150px;"
"            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);"
"            border-radius: 10px;"
"            margin: 20px auto;"
"            border: 2px solid #667eea;"
"            display: flex;"
"            align-items: center;"
"            justify-content: center;"
"        }"
"        .joystick-bg {"
"            position: absolute;"
"            width: 100%;"
"            height: 100%;"
"            background: radial-gradient(circle, transparent 0%, rgba(102,126,234,0.1) 100%);"
"            border-radius: 10px;"
"            display: grid;"
"            grid-template-columns: repeat(3, 1fr);"
"            grid-template-rows: repeat(3, 1fr);"
"        }"
"        .grid-cell { border: 1px solid rgba(102,126,234,0.2); }"
"        .joystick-stick {"
"            width: 60px;"
"            height: 60px;"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"            border-radius: 50%;"
"            position: absolute;"
"            z-index: 10;"
"            box-shadow: 0 5px 15px rgba(102,126,234,0.4);"
"            cursor: grab;"
"        }"
"        .motor-control {"
"            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);"
"            padding: 15px;"
"            border-radius: 8px;"
"            margin-bottom: 15px;"
"        }"
"        .motor-title {"
"            font-weight: 600;"
"            color: #333;"
"            margin-bottom: 10px;"
"            font-size: 14px;"
"        }"
"        .speed-bar {"
"            width: 100%;"
"            height: 25px;"
"            background: #e0e0e0;"
"            border-radius: 5px;"
"            overflow: hidden;"
"            position: relative;"
"            margin-bottom: 8px;"
"        }"
"        .speed-fill {"
"            height: 100%;"
"            background: linear-gradient(90deg, #667eea 0%, #764ba2 100%);"
"            width: 50%;"
"            transition: width 0.1s;"
"            display: flex;"
"            align-items: center;"
"            justify-content: center;"
"            color: white;"
"            font-size: 12px;"
"            font-weight: bold;"
"        }"
"        .speed-text {"
"            font-size: 12px;"
"            color: #666;"
"        }"
"        .stats-grid {"
"            display: grid;"
"            grid-template-columns: 1fr 1fr;"
"            gap: 10px;"
"        }"
"        .stat-item {"
"            background: white;"
"            padding: 10px;"
"            border-radius: 5px;"
"            text-align: center;"
"            border: 1px solid #e0e0e0;"
"        }"
"        .stat-label {"
"            font-size: 11px;"
"            color: #999;"
"        }"
"        .stat-value {"
"            font-size: 16px;"
"            font-weight: bold;"
"            color: #667eea;"
"            margin-top: 5px;"
"        }"
"        .status-indicator {"
"            display: inline-block;"
"            width: 12px;"
"            height: 12px;"
"            border-radius: 50%;"
"            margin-right: 8px;"
"            animation: pulse 1s infinite;"
"        }"
"        .status-indicator.connected {"
"            background: #4caf50;"
"        }"
"        .status-indicator.disconnected {"
"            background: #f44336;"
"            animation: none;"
"        }"
"        @keyframes pulse {"
"            0%, 100% { opacity: 1; }"
"            50% { opacity: 0.5; }"
"        }"
"        @media (max-width: 768px) {"
"            .content {"
"                grid-template-columns: 1fr;"
"            }"
"        }"
"    </style>"
"</head>"
"<body>"
"    <div class=\"container\">"
"        <div class=\"header\">"
"            <h1>🎮 Remote Transmitter Control</h1>"
"            <p>ESP-NOW Long Range Remote Controller</p>"
"        </div>"
"        <div class=\"content\">"
"            <div class=\"card\">"
"                <h2>Joystick Control</h2>"
"                <div style=\"text-align: center;\">"
"                    <p style=\"color: #666; font-size: 13px; margin-bottom: 10px;\">Motor 1</p>"
"                    <div class=\"joystick-container\">"
"                        <div class=\"joystick-bg\"></div>"
"                        <div class=\"joystick-stick\" id=\"stick1\" style=\"left: 50%; top: 50%; transform: translate(-50%, -50%);\"></div>"
"                    </div>"
"                </div>"
"                <div class=\"motor-control\">"
"                    <div class=\"motor-title\">Motor 1 Speed</div>"
"                    <div class=\"speed-bar\">"
"                        <div class=\"speed-fill\" id=\"motor1-bar\" style=\"width: 50%;\">"
"                            <span id=\"motor1-value\">0</span>"
"                        </div>"
"                    </div>"
"                    <div class=\"speed-text\">Direction: <span id=\"motor1-dir\">Stop</span></div>"
"                </div>"
"            </div>"
"            <div class=\"card\">"
"                <h2>System Status & Settings</h2>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">WiFi Status</div>"
"                    <div class=\"status-value\">"
"                        <span class=\"status-indicator connected\" id=\"wifi-status\"></span>"
"                        <span id=\"wifi-text\">Connected</span>"
"                    </div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Network</div>"
"                    <div class=\"status-value\" id=\"network-info\">SLEngineers</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Local IP</div>"
"                    <div class=\"status-value\" id=\"local-ip\">192.168.1.100</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">ESP-NOW Long Range</div>"
"                    <div class=\"status-value\" id=\"long-range\">Enabled</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Transmission Stats</div>"
"                    <div class=\"stats-grid\">"
"                        <div class=\"stat-item\">"
"                            <div class=\"stat-label\">Packets Sent</div>"
"                            <div class=\"stat-value\" id=\"packets-sent\">0</div>"
"                        </div>"
"                        <div class=\"stat-item\">"
"                            <div class=\"stat-label\">Update Rate</div>"
"                            <div class=\"stat-value\" id=\"update-rate\">0 Hz</div>"
"                        </div>"
"                    </div>"
"                </div>"
"            </div>"
"            <div class=\"card\">"
"                <h2>Motor 2 Control</h2>"
"                <div style=\"text-align: center;\">"
"                    <p style=\"color: #666; font-size: 13px; margin-bottom: 10px;\">Motor 2</p>"
"                    <div class=\"joystick-container\">"
"                        <div class=\"joystick-bg\"></div>"
"                        <div class=\"joystick-stick\" id=\"stick2\" style=\"left: 50%; top: 50%; transform: translate(-50%, -50%);\"></div>"
"                    </div>"
"                </div>"
"                <div class=\"motor-control\">"
"                    <div class=\"motor-title\">Motor 2 Speed</div>"
"                    <div class=\"speed-bar\">"
"                        <div class=\"speed-fill\" id=\"motor2-bar\" style=\"width: 50%;\">"
"                            <span id=\"motor2-value\">0</span>"
"                        </div>"
"                    </div>"
"                    <div class=\"speed-text\">Direction: <span id=\"motor2-dir\">Stop</span></div>"
"                </div>"
"            </div>"
"            <div class=\"card\">"
"                <h2>ADC Joystick Readings</h2>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Joystick 1 - X Axis</div>"
"                    <div class=\"status-value\" id=\"adc-x\">2048</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Joystick 1 - Y Axis</div>"
"                    <div class=\"status-value\" id=\"adc-y\">2048</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Joystick 2 - X Axis</div>"
"                    <div class=\"status-value\" id=\"adc-x2\">2048</div>"
"                </div>"
"                <div class=\"status-group\">"
"                    <div class=\"status-label\">Joystick 2 - Y Axis</div>"
"                    <div class=\"status-value\" id=\"adc-y2\">2048</div>"
"                </div>"
"            </div>"
"        </div>"
"    </div>"
"    <script>"
"        let lastUpdate = Date.now();"
"        let updateCount = 0;"
"        const maxUpdates = 10;"
"        "
"        async function updateStatus() {"
"            try {"
"                const response = await fetch('/api/status');"
"                const data = await response.json();"
"                "
"                // Update motor data"
"                document.getElementById('motor1-value').textContent = data.motor1_speed;"
"                document.getElementById('motor1-dir').textContent = ["
"                    'Stop', 'Forward', 'Reverse'"
"                ][data.motor1_direction + 1];"
"                document.getElementById('motor2-value').textContent = data.motor2_speed;"
"                document.getElementById('motor2-dir').textContent = ["
"                    'Stop', 'Forward', 'Reverse'"
"                ][data.motor2_direction + 1];"
"                "
"                // Update motor bars"
"                const m1percent = ((data.motor1_speed + 127) / 254) * 100;"
"                const m2percent = ((data.motor2_speed + 127) / 254) * 100;"
"                document.getElementById('motor1-bar').style.width = m1percent + '%';"
"                document.getElementById('motor2-bar').style.width = m2percent + '%';"
"                "
"                // Update ADC values"
"                document.getElementById('adc-x').textContent = data.adc_x;"
"                document.getElementById('adc-y').textContent = data.adc_y;"
"                document.getElementById('adc-x2').textContent = data.adc_x2;"
"                document.getElementById('adc-y2').textContent = data.adc_y2;"
"                "
"                // Update joystick positions"
"                const stick1x = (data.adc_x / 4095) * 100;"
"                const stick1y = (data.adc_y / 4095) * 100;"
"                document.getElementById('stick1').style.left = stick1x + '%';"
"                document.getElementById('stick1').style.top = stick1y + '%';"
"                "
"                const stick2x = (data.adc_x2 / 4095) * 100;"
"                const stick2y = (data.adc_y2 / 4095) * 100;"
"                document.getElementById('stick2').style.left = stick2x + '%';"
"                document.getElementById('stick2').style.top = stick2y + '%';"
"                "
"                // Update rate calculation"
"                updateCount++;"
"                const now = Date.now();"
"                if (now - lastUpdate >= 1000) {"
"                    document.getElementById('update-rate').textContent = "
"                        (updateCount * 1000 / (now - lastUpdate)).toFixed(1) + ' Hz';"
"                    updateCount = 0;"
"                    lastUpdate = now;"
"                }"
"            } catch (error) {"
"                console.error('Update failed:', error);"
"            }"
"        }"
"        "
"        // Update every 100ms"
"        setInterval(updateStatus, 100);"
"        updateStatus();"
"    </script>"
"</body>"
"</html>";

/**
 * HTTP GET handler for root page
 */
static esp_err_t http_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html_content, strlen(html_content));
}

/**
 * HTTP GET handler for API status
 */
static esp_err_t http_api_status_handler(httpd_req_t *req)
{
    // Build JSON response manually
    char json_buffer[512];
    
    snprintf(json_buffer, sizeof(json_buffer),
        "{"
        "\"motor1_speed\":%d,"
        "\"motor1_direction\":%d,"
        "\"motor2_speed\":%d,"
        "\"motor2_direction\":%d,"
        "\"adc_x\":%u,"
        "\"adc_y\":%u,"
        "\"adc_x2\":%u,"
        "\"adc_y2\":%u,"
        "\"long_range_enabled\":%u,"
        "\"wifi_connected\":1,"
        "\"wifi_ssid\":\"%s\""
        "}",
        current_motor_data.motor1_speed,
        current_motor_data.motor1_direction,
        current_motor_data.motor2_speed,
        current_motor_data.motor2_direction,
        adc_x, adc_y, adc_x2, adc_y2,
        current_settings.long_range_enabled,
        current_settings.wifi_ssid
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buffer, strlen(json_buffer));
    
    
    return ESP_OK;
}

/**
 * URI handlers
 */
static const httpd_uri_t uri_get_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = http_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = http_api_status_handler,
    .user_ctx = NULL
};

/**
 * Initialize HTTP web server
 */
int web_server_init(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG, "Starting HTTP Server on port %d", port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get_root);
        httpd_register_uri_handler(server, &uri_api_status);
        ESP_LOGI(TAG, "Web server started successfully");
        return 0;
    }

    ESP_LOGE(TAG, "Failed to start HTTP Server");
    return -1;
}

/**
 * Update remote settings
 */
void web_server_update_settings(const remote_settings_t *settings)
{
    if (settings) {
        memcpy(&current_settings, settings, sizeof(remote_settings_t));
    }
}

/**
 * Update motor control data
 */
void web_server_update_motor_data(const motor_control_t *motor_data)
{
    if (motor_data) {
        memcpy(&current_motor_data, motor_data, sizeof(motor_control_t));
    }
}

/**
 * Update ADC readings for display
 */
void web_server_update_adc_values(uint16_t x, uint16_t y, uint16_t x2, uint16_t y2)
{
    adc_x = x;
    adc_y = y;
    adc_x2 = x2;
    adc_y2 = y2;
}

/**
 * Stop web server
 */
void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
