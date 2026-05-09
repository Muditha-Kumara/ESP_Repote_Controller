#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;

static motor_control_t current_motor_data = {0};
static remote_settings_t current_settings = {0};
static int16_t current_throttle = 0;
static int16_t current_steer = 0;
static uint8_t current_armed = 0;

static const char index_html[] =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>TransMeter Boat Pilot</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:system-ui,sans-serif;min-height:100vh;color:#fff;background:radial-gradient(circle at top,#48b0ff 0,#0d1b3d 55%,#06101f 100%);overflow-x:hidden}"
    "body:before,body:after{content:'';position:fixed;inset:auto;border-radius:50%;filter:blur(20px);opacity:.45;pointer-events:none}"
    "body:before{width:260px;height:260px;left:-70px;top:20px;background:#4de1ff}"
    "body:after{width:320px;height:320px;right:-120px;bottom:-60px;background:#ffcb4d}"
    ".wrap{position:relative;max-width:1100px;margin:0 auto;padding:20px}"
    ".hero{padding:18px 16px 10px;text-align:center}"
    ".hero h1{font-size:clamp(2rem,5vw,4.2rem);letter-spacing:.04em;line-height:1.05}"
    ".hero p{margin-top:10px;font-size:clamp(1rem,2vw,1.25rem);opacity:.9}"
    ".badge{display:inline-flex;align-items:center;gap:10px;margin-top:14px;padding:10px 16px;border-radius:999px;background:rgba(255,255,255,.12);backdrop-filter:blur(12px);border:1px solid rgba(255,255,255,.2)}"
    ".dot{width:14px;height:14px;border-radius:50%;background:#ff6b6b;box-shadow:0 0 0 0 rgba(255,107,107,.7);animation:pulse 1.5s infinite}"
    ".dot.on{background:#3dff9f;box-shadow:0 0 0 0 rgba(61,255,159,.7)}"
    "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(255,255,255,.45)}70%{box-shadow:0 0 0 16px rgba(255,255,255,0)}100%{box-shadow:0 0 0 0 rgba(255,255,255,0)}}"
    ".grid{display:grid;grid-template-columns:1.1fr .9fr;gap:18px;margin-top:18px}"
    ".card{background:rgba(9,18,40,.55);border:1px solid rgba(255,255,255,.16);border-radius:28px;backdrop-filter:blur(16px);box-shadow:0 25px 70px rgba(0,0,0,.32);overflow:hidden}"
    ".card .inner{padding:18px}"
    ".pad{position:relative;margin:10px auto 6px;width:min(82vw,380px);aspect-ratio:1;border-radius:34px;background:linear-gradient(145deg,rgba(255,255,255,.14),rgba(255,255,255,.05));border:2px solid rgba(255,255,255,.2);touch-action:none;user-select:none}"
    ".pad:before,.pad:after{content:'';position:absolute;background:rgba(255,255,255,.16)}"
    ".pad:before{inset:50% 18px auto 18px;height:2px;transform:translateY(-1px)}"
    ".pad:after{left:50%;top:18px;bottom:18px;width:2px;transform:translateX(-1px)}"
    ".glow{position:absolute;inset:18px;border-radius:26px;background:radial-gradient(circle at center,rgba(77,225,255,.18),transparent 60%)}"
    ".puck{position:absolute;left:50%;top:50%;width:96px;height:96px;border-radius:28px;transform:translate(-50%,-50%);background:linear-gradient(135deg,#fff,#8fd3ff);box-shadow:0 18px 40px rgba(0,0,0,.35);display:grid;place-items:center;color:#0c1f3f;font-weight:900;font-size:18px;letter-spacing:.06em}"
    ".puck small{display:block;font-size:11px;opacity:.75;letter-spacing:.18em}"
    ".stats{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-top:14px}"
    ".stat{padding:14px;border-radius:22px;background:rgba(255,255,255,.09);border:1px solid rgba(255,255,255,.12)}"
    ".label{font-size:.78rem;opacity:.76;text-transform:uppercase;letter-spacing:.14em}"
    ".value{margin-top:8px;font-size:1.65rem;font-weight:800}"
    ".motor-bars{display:grid;gap:14px;margin-top:8px}"
    ".bar{position:relative;height:26px;border-radius:999px;background:rgba(255,255,255,.12);overflow:hidden}"
    ".fill{height:100%;width:50%;border-radius:999px;transition:width .08s linear;background:linear-gradient(90deg,#41d1ff,#4dff9a)}"
    ".bar span{position:absolute;inset:0;display:grid;place-items:center;font-size:.85rem;font-weight:800;color:#0a1933;text-shadow:0 1px 0 rgba(255,255,255,.5)}"
    ".controls{display:flex;gap:12px;flex-wrap:wrap;margin-top:16px}"
    "button{border:0;border-radius:20px;padding:16px 18px;font-size:1rem;font-weight:900;cursor:pointer;min-width:140px;box-shadow:0 10px 25px rgba(0,0,0,.24)}"
    ".arm{background:linear-gradient(135deg,#34ffa5,#13c77d);color:#031318}"
    ".stop{background:linear-gradient(135deg,#ff7c5c,#ff4d6d);color:#fff}"
    ".hint{margin-top:12px;font-size:.95rem;line-height:1.45;opacity:.88}"
    ".mini{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}"
    ".chip{padding:12px 10px;border-radius:18px;background:rgba(255,255,255,.09);text-align:center;font-weight:800}"
    "@media (max-width:860px){.grid{grid-template-columns:1fr}.stats{grid-template-columns:1fr 1fr}.puck{width:84px;height:84px}}"
    "@media (max-width:520px){.wrap{padding:14px}.card{border-radius:22px}.stats,.mini{grid-template-columns:1fr}.controls button{flex:1;min-width:0}}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='wrap'>"
    "<div class='hero'>"
    "<h1>Boat Pilot</h1>"
    "<p>Drag the pad to drive forward, backward, left, and right.</p>"
    "<div class='badge'><span id='connDot' class='dot'></span><span id='connText'>Connecting to boat...</span></div>"
    "</div>"
    "<div class='grid'>"
    "<section class='card'><div class='inner'>"
    "<div class='label'>Touch control pad</div>"
    "<div id='pad' class='pad'><div class='glow'></div><div id='puck' class='puck'><div><small>BOAT</small>GO</div></div></div>"
    "<div class='controls'><button class='arm' id='armBtn'>ARM BOAT</button><button class='stop' id='stopBtn'>STOP</button></div>"
    "<div class='hint'>Up and down control thrust. Left and right control steering. Release to go back to center.</div>"
    "</div></section>"
    "<section class='card'><div class='inner'>"
    "<div class='label'>Live status</div>"
    "<div class='stats'>"
    "<div class='stat'><div class='label'>Thrust motor</div><div class='value' id='throttleTxt'>0%</div></div>"
    "<div class='stat'><div class='label'>Steering motor</div><div class='value' id='steerTxt'>0%</div></div>"
    "<div class='stat'><div class='label'>Thrust direction</div><div class='value' id='leftTxt'>Stop</div></div>"
    "<div class='stat'><div class='label'>Steer direction</div><div class='value' id='rightTxt'>Center</div></div>"
    "</div>"
    "<div class='motor-bars'>"
    "<div><div class='label'>Thrust level</div><div class='bar'><div id='leftBar' class='fill'></div><span id='leftBarTxt'>0</span></div></div>"
    "<div><div class='label'>Steering level</div><div class='bar'><div id='rightBar' class='fill'></div><span id='rightBarTxt'>0</span></div></div>"
    "</div>"
    "<div class='mini'>"
    "<div class='chip' id='wifiChip'>WiFi: waiting</div>"
    "<div class='chip' id='modeChip'>Mode: idle</div>"
    "<div class='chip' id='packetsChip'>Packets: 0</div>"
    "</div>"
    "</div></section>"
    "</div></div>"
    "<script>"
    "let ws=null, armed=false, lastSend=0, packets=0, throttle=0, steer=0;"
    "const pad=document.getElementById('pad'), puck=document.getElementById('puck');"
    "const connDot=document.getElementById('connDot'), connText=document.getElementById('connText');"
    "const throttleTxt=document.getElementById('throttleTxt'), steerTxt=document.getElementById('steerTxt');"
    "const leftTxt=document.getElementById('leftTxt'), rightTxt=document.getElementById('rightTxt');"
    "const leftBar=document.getElementById('leftBar'), rightBar=document.getElementById('rightBar');"
    "const leftBarTxt=document.getElementById('leftBarTxt'), rightBarTxt=document.getElementById('rightBarTxt');"
    "const armBtn=document.getElementById('armBtn'), stopBtn=document.getElementById('stopBtn');"
    "const wifiChip=document.getElementById('wifiChip'), modeChip=document.getElementById('modeChip'), packetsChip=document.getElementById('packetsChip');"
    "function clamp(v,min,max){return Math.min(max,Math.max(min,v));}"
    "function setConnected(ok,text){connDot.className='dot'+(ok?' on':'');connText.textContent=text;}"
    "function motorStyle(value){return clamp(Math.abs(value),0,127)/127*100+'%';}"
    "function motorText(value){if(value===0) return 'Stop'; return value>0 ? 'Forward' : 'Reverse';}"
    "function steerText(value){if(value===0) return 'Center'; return value>0 ? 'Right' : 'Left';}"
    "function render(){throttleTxt.textContent=Math.abs(throttle)+'%'; steerTxt.textContent=Math.abs(steer)+'%'; leftTxt.textContent=motorText(throttle); rightTxt.textContent=steerText(steer); leftBar.style.width=motorStyle(throttle); rightBar.style.width=motorStyle(steer); leftBarTxt.textContent=Math.abs(throttle); rightBarTxt.textContent=Math.abs(steer); modeChip.textContent='Mode: '+(armed?'armed':'safe');}"
    "function sendState(force){if(!ws||ws.readyState!==1) return; const now=Date.now(); if(!force && now-lastSend<40) return; lastSend=now; const msg={type:'drive',throttle:throttle|0,steer:steer|0,armed:armed?1:0}; ws.send(JSON.stringify(msg));}"
    "function stopBoat(){throttle=0;steer=0;armed=false;render();sendState(true); armBtn.textContent='ARM BOAT';}"
    "function setFromPointer(ev){const r=pad.getBoundingClientRect(); const x=clamp((ev.clientX-r.left)/r.width,0,1)*2-1; const y=clamp((ev.clientY-r.top)/r.height,0,1)*2-1; steer=Math.round(x*127); throttle=Math.round(-y*127); const puckX=((x*0.5)+0.5)*(r.width-96)+48; const puckY=((y*0.5)+0.5)*(r.height-96)+48; puck.style.left=puckX+'px'; puck.style.top=puckY+'px'; render(); sendState(false);}"
    "function centerPad(){const r=pad.getBoundingClientRect(); steer=0; throttle=0; puck.style.left='50%'; puck.style.top='50%'; render(); sendState(true);}"
    "pad.addEventListener('pointerdown',e=>{if(!armed) return; pad.setPointerCapture(e.pointerId); setFromPointer(e);});"
    "pad.addEventListener('pointermove',e=>{if(!armed||e.buttons===0) return; setFromPointer(e);});"
    "pad.addEventListener('pointerup',()=>centerPad()); pad.addEventListener('pointercancel',()=>centerPad()); pad.addEventListener('pointerleave',()=>{if(armed) centerPad();});"
    "armBtn.onclick=()=>{armed=!armed; armBtn.textContent=armed?'DISARM BOAT':'ARM BOAT'; render(); sendState(true);};"
    "stopBtn.onclick=()=>stopBoat();"
    "function connect(){const proto=location.protocol==='https:'?'wss':'ws'; ws=new WebSocket(proto+'://'+location.host+'/ws'); ws.onopen=()=>{setConnected(true,'Boat link ready'); wifiChip.textContent='WiFi: connected'; sendState(true);}; ws.onclose=()=>{setConnected(false,'Reconnecting...'); wifiChip.textContent='WiFi: offline'; setTimeout(connect,1200);}; ws.onerror=()=>{setConnected(false,'Link error');}; ws.onmessage=ev=>{try{const d=JSON.parse(ev.data); if(typeof d.throttle==='number') throttle=d.throttle; if(typeof d.steer==='number') steer=d.steer; if(typeof d.armed!=='undefined') armed=!!d.armed; if(d.packets!==undefined){packets=d.packets; packetsChip.textContent='Packets: '+packets;} if(d.left!==undefined&&d.right!==undefined){leftTxt.textContent=d.left; rightTxt.textContent=d.right;} render();}catch(_e){}};}"
    "render(); centerPad(); connect(); setInterval(()=>sendState(false),100);"
    "</script></body></html>";

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static int8_t clamp_int8(int value)
{
    return (int8_t)clamp_int(value, -127, 127);
}

static void command_to_motor(int16_t throttle, int16_t steer, motor_control_t *motor_data)
{
    motor_data->motor1_speed = clamp_int8((clamp_int(throttle, -100, 100) * 127) / 100);
    motor_data->motor1_direction = (motor_data->motor1_speed > 0) ? 1 : (motor_data->motor1_speed < 0 ? -1 : 0);

    motor_data->motor2_speed = clamp_int8((clamp_int(abs(steer), 0, 100) * 127) / 100);
    motor_data->motor2_direction = (steer > 0) ? 1 : (steer < 0 ? -1 : 0);
    motor_data->timestamp = esp_log_timestamp();
}

static bool json_get_int(const char *json, const char *key, int *value)
{
    char pattern[24];
    const char *cursor = NULL;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    cursor = strstr(json, pattern);
    if (cursor == NULL)
    {
        return false;
    }

    cursor += strlen(pattern);
    *value = (int)strtol(cursor, NULL, 10);
    return true;
}

static void update_control_state(int16_t throttle, int16_t steer, uint8_t armed)
{
    current_throttle = clamp_int(throttle, -100, 100);
    current_steer = clamp_int(steer, -100, 100);
    current_armed = armed ? 1 : 0;

    if (!current_armed)
    {
        current_throttle = 0;
        current_steer = 0;
    }

    command_to_motor(current_throttle, current_steer, &current_motor_data);
    if (!current_armed)
    {
        current_motor_data.motor1_speed = 0;
        current_motor_data.motor2_speed = 0;
        current_motor_data.motor1_direction = 0;
        current_motor_data.motor2_direction = 0;
    }
}

static esp_err_t http_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_api_status_handler(httpd_req_t *req)
{
    char json_buffer[256];
    snprintf(json_buffer, sizeof(json_buffer),
             "{\"throttle\":%d,\"steer\":%d,\"armed\":%u,\"motor1_speed\":%d,\"motor1_direction\":%d,\"motor2_speed\":%d,\"motor2_direction\":%d,\"packets\":%lu,\"wifi_ssid\":\"%s\",\"long_range_enabled\":%u}",
             current_throttle,
             current_steer,
             current_armed,
             current_motor_data.motor1_speed,
             current_motor_data.motor1_direction,
             current_motor_data.motor2_speed,
             current_motor_data.motor2_direction,
             (unsigned long)current_motor_data.timestamp,
             current_settings.wifi_ssid,
             current_settings.long_range_enabled);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_buffer, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WebSocket client connected");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to read websocket frame length: %s", esp_err_to_name(ret));
        return ret;
    }

    if (frame.len == 0 || frame.len > 255)
    {
        return ESP_FAIL;
    }

    uint8_t payload[256] = {0};
    frame.payload = payload;
    ret = httpd_ws_recv_frame(req, &frame, sizeof(payload) - 1);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to read websocket payload: %s", esp_err_to_name(ret));
        return ret;
    }

    payload[frame.len] = '\0';

    int throttle = current_throttle;
    int steer = current_steer;
    int armed = current_armed;

    (void)json_get_int((const char *)payload, "throttle", &throttle);
    (void)json_get_int((const char *)payload, "steer", &steer);
    (void)json_get_int((const char *)payload, "armed", &armed);

    update_control_state((int16_t)throttle, (int16_t)steer, (uint8_t)armed);

    char response[256];
    snprintf(response, sizeof(response),
             "{\"throttle\":%d,\"steer\":%d,\"armed\":%u,\"left\":%d,\"right\":%d,\"packets\":%lu}",
             current_throttle,
             current_steer,
             current_armed,
             current_motor_data.motor1_speed,
             current_motor_data.motor2_speed,
             (unsigned long)current_motor_data.timestamp);

    httpd_ws_frame_t out = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)response,
        .len = strlen(response),
    };

    return httpd_ws_send_frame(req, &out);
}

static const httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = http_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = http_api_status_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

int web_server_init(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = 8192;
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting websocket web server on port %u", port);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = httpd_register_uri_handler(server, &uri_root);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register root handler: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = httpd_register_uri_handler(server, &uri_status);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register status handler: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = httpd_register_uri_handler(server, &uri_ws);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register websocket handler: %s", esp_err_to_name(ret));
        return -1;
    }

    update_control_state(0, 0, 0);
    ESP_LOGI(TAG, "Web server started successfully");
    return 0;
}

void web_server_update_settings(const remote_settings_t *settings)
{
    if (settings != NULL)
    {
        memcpy(&current_settings, settings, sizeof(current_settings));
    }
}

void web_server_update_motor_data(const motor_control_t *motor_data)
{
    if (motor_data != NULL)
    {
        memcpy(&current_motor_data, motor_data, sizeof(current_motor_data));
    }
}

void web_server_update_drive_command(int16_t throttle, int16_t steer, uint8_t armed)
{
    update_control_state(throttle, steer, armed);
}

void web_server_get_motor_data(motor_control_t *motor_data)
{
    if (motor_data != NULL)
    {
        memcpy(motor_data, &current_motor_data, sizeof(*motor_data));
    }
}

void web_server_stop(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}