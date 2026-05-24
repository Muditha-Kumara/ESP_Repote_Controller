#include "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;

static motor_control_t current_motor_data = {0};
static remote_settings_t current_settings = {0};
static uint8_t current_ap_channel = 0;
static uint32_t current_packet_count = 0;
static uint8_t current_joystick_connected = 0;
static uint8_t current_joystick_tx_enabled = 0;
static int16_t current_joystick_lx = 0;
static int16_t current_joystick_ly = 0;
static int8_t current_receiver_rssi_dbm = 0;
static float current_estimated_distance_m = -1.0f;
static uint32_t current_rtt_us = 0;
static uint8_t current_link_valid = 0;

static const char index_html[] =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>TransMeter Boat Explorer</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:system-ui,sans-serif;min-height:100vh;color:#fff;background:radial-gradient(circle at top,#13315b 0,#08111f 52%,#02040a 100%);overflow-x:hidden}"
    "body:before,body:after{content:'';position:fixed;border-radius:50%;filter:blur(48px);opacity:.25;pointer-events:none}"
    "body:before{width:280px;height:280px;left:-60px;top:10%;background:#38bdf8}"
    "body:after{width:360px;height:360px;right:-110px;bottom:5%;background:#fbbf24}"
    ".wrap{max-width:1180px;margin:0 auto;padding:18px;position:relative}"
    ".hero{text-align:center;padding:8px 8px 16px}"
    ".hero h1{font-size:clamp(2.2rem,5vw,4.5rem);font-weight:900;letter-spacing:-0.03em;background:linear-gradient(90deg,#7dd3fc 0,#fbbf24 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent}"
    ".hero p{max-width:780px;margin:10px auto 0;font-size:clamp(1rem,1.8vw,1.15rem);color:#b6c2d1;line-height:1.55}"
    ".badge{display:inline-flex;align-items:center;gap:10px;margin-top:14px;padding:10px 16px;border-radius:999px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.12);backdrop-filter:blur(12px)}"
    ".dot{width:12px;height:12px;border-radius:50%;background:#ef4444;box-shadow:0 0 0 0 rgba(239,68,68,.45)}"
    ".dot.on{background:#22c55e;animation:pulse 2s infinite}"
    "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(34,197,94,.55)}70%{box-shadow:0 0 0 10px rgba(34,197,94,0)}100%{box-shadow:0 0 0 0 rgba(34,197,94,0)}}"
    ".grid{display:grid;grid-template-columns:1.15fr .85fr;gap:18px;margin-top:14px}"
    ".card{background:rgba(8,14,26,.62);border:1px solid rgba(255,255,255,.08);border-radius:26px;backdrop-filter:blur(18px);box-shadow:0 22px 60px rgba(0,0,0,.35);overflow:hidden}"
    ".inner{padding:18px}"
    ".scene{position:relative;min-height:430px;border-radius:22px;overflow:hidden;background:linear-gradient(180deg,#0f172a 0%,#123b73 68%,#07101f 100%);border:1px solid rgba(255,255,255,.06)}"
    ".sky-glow{position:absolute;right:24px;top:20px;width:100px;height:100px;border-radius:50%;background:radial-gradient(circle,#fff7c8 0,#fbbf24 35%,rgba(251,191,36,0) 70%);opacity:.9}"
    ".water{position:absolute;left:0;right:0;bottom:0;height:42%;background:linear-gradient(180deg,rgba(34,211,238,.12) 0,rgba(14,165,233,.38) 36%,rgba(6,37,83,.9) 100%)}"
    ".water:before,.water:after{content:'';position:absolute;left:-10%;right:-10%;height:44px;border-radius:50%;background:radial-gradient(circle at 50% 0,rgba(255,255,255,.18),rgba(255,255,255,0) 70%)}"
    ".water:before{top:-12px;animation:wave 5s ease-in-out infinite}"
    ".water:after{top:10px;opacity:.7;animation:wave 6.5s ease-in-out infinite reverse}"
    "@keyframes wave{0%,100%{transform:translateX(-2%) scaleX(1)}50%{transform:translateX(2%) scaleX(1.05)}}"
    ".boat-wrap{position:absolute;left:50%;bottom:13%;width:min(92vw,680px);transform:translateX(-50%)}"
    ".boat-label{display:flex;justify-content:center;gap:10px;margin-bottom:12px;font-weight:900;letter-spacing:.12em;text-transform:uppercase;opacity:.95}"
    ".boat{position:relative;height:220px;transition:transform .12s ease;filter:drop-shadow(0 18px 26px rgba(0,0,0,.35))}"
    ".boat-body{position:absolute;left:50%;bottom:22px;width:min(100%,640px);height:128px;transform:translateX(-50%);background:linear-gradient(180deg,#7c3f19,#4f240b);clip-path:polygon(8% 16%,92% 16%,100% 50%,92% 84%,8% 84%,0 50%);border:3px solid rgba(255,255,255,.14);border-radius:24px}"
    ".boat-deck{position:absolute;left:50%;bottom:62px;width:min(88%,548px);height:64px;transform:translateX(-50%);background:linear-gradient(180deg,#ffe08b,#f4a93c);clip-path:polygon(6% 22%,94% 22%,100% 50%,94% 78%,6% 78%,0 50%);border:3px solid rgba(255,255,255,.18);border-radius:18px}"
    ".cabin{position:absolute;left:50%;bottom:95px;width:180px;height:82px;transform:translateX(-50%);background:linear-gradient(180deg,#f8fcff,#bcecff);border-radius:22px 22px 18px 18px;border:3px solid rgba(255,255,255,.35)}"
    ".window{position:absolute;top:14px;width:34px;height:26px;border-radius:10px;background:linear-gradient(180deg,#86d8ff,#1e69ab);box-shadow:inset 0 -8px 16px rgba(255,255,255,.24)}"
    ".window.left{left:28px}.window.right{right:28px}"
    ".smoke{position:absolute;left:50%;bottom:155px;width:16px;height:16px;border-radius:50%;background:rgba(255,255,255,.45);transform:translateX(-50%);opacity:0}"
    ".smoke.active{animation:smoke 3.2s ease-in-out infinite}"
    ".smoke.second{animation-delay:.7s}"
    "@keyframes smoke{0%,100%{opacity:0;transform:translateX(-50%) translateY(0) scale(.45)}25%{opacity:.8}65%{opacity:.25;transform:translateX(-50%) translateY(-16px) scale(1.1)}}"
    ".motor{position:absolute;bottom:12px;width:188px;height:90px;border-radius:20px;background:linear-gradient(180deg,#3f4d5f,#202833);border:2px solid rgba(255,255,255,.16);cursor:pointer;transition:transform .15s ease,box-shadow .15s ease}"
    ".motor:hover{transform:translateY(-4px) scale(1.02)}"
    ".motor.active{box-shadow:0 0 0 3px rgba(109,239,255,.2),0 0 30px rgba(109,239,255,.25)}"
    ".motor.left{left:38px}.motor.right{right:38px}"
    ".motor .tag{position:absolute;left:12px;top:8px;font-size:.7rem;font-weight:900;letter-spacing:.12em;text-transform:uppercase;opacity:.8}"
    ".motor .power{position:absolute;left:12px;bottom:10px;font-size:2rem;font-weight:900;line-height:1}"
    ".motor .bar{position:absolute;right:12px;bottom:14px;width:92px;height:14px;border-radius:999px;background:rgba(255,255,255,.14);overflow:hidden}"
    ".motor .bar i{display:block;height:100%;width:0;border-radius:999px;background:linear-gradient(90deg,#4dffb0,#6ee8ff,#ffd86a);transition:width .12s linear}"
    ".propeller{position:absolute;top:16px;width:46px;height:46px;border-radius:50%;background:radial-gradient(circle,#d9f4ff 0,#8ab9d3 55%,#43586b 100%);border:2px solid rgba(255,255,255,.22)}"
    ".propeller:before,.propeller:after{content:'';position:absolute;left:50%;top:50%;width:18px;height:48px;border-radius:50% 50% 45% 45%;background:linear-gradient(180deg,#fff,#85d8ff);transform-origin:center center}"
    ".propeller:before{transform:translate(-50%,-50%) rotate(0deg)}"
    ".propeller:after{transform:translate(-50%,-50%) rotate(90deg)}"
    ".motor.active .propeller{animation:spin .7s linear infinite}"
    ".motor.left .propeller{right:-14px}.motor.right .propeller{left:-14px}"
    "@keyframes spin{to{transform:rotate(360deg)}}"
    ".wake{position:absolute;left:50%;bottom:16px;width:min(92vw,680px);height:96px;transform:translateX(-50%);pointer-events:none}"
    ".bubble{position:absolute;bottom:10px;width:8px;height:8px;border-radius:50%;background:rgba(255,255,255,.9);opacity:0;animation:bubble 4s linear infinite}"
    "@keyframes bubble{0%{transform:translateY(0) scale(.5);opacity:0}15%{opacity:.9}100%{transform:translateY(-78px) scale(1.5);opacity:0}}"
    ".hint-row{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}"
    ".pill{padding:9px 12px;border-radius:999px;background:rgba(255,255,255,.1);font-size:.86rem;font-weight:800}"
    ".right-top{display:grid;grid-template-columns:1fr;gap:12px}"
    ".signal-card{padding:16px;border-radius:22px;background:linear-gradient(180deg,rgba(255,255,255,.1),rgba(255,255,255,.05));border:1px solid rgba(255,255,255,.12)}"
    ".signal-head{display:flex;justify-content:space-between;gap:10px;align-items:center}"
    ".signal-head strong{font-size:.85rem;letter-spacing:.12em;text-transform:uppercase;opacity:.72}"
    ".signal-head span{font-size:1.35rem;font-weight:900}"
    ".meter{margin-top:12px;height:18px;border-radius:999px;background:rgba(255,255,255,.12);overflow:hidden}"
    ".meter i{display:block;height:100%;width:0;background:linear-gradient(90deg,#ef4444 0,#f59e0b 35%,#fbbf24 60%,#22c55e 100%);transition:width .18s ease}"
    ".subtle{margin-top:8px;color:#c8d5e5;font-size:.9rem;line-height:1.45}"
    ".stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
    ".stat{padding:12px;border-radius:18px;background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1)}"
    ".stat .k{display:block;font-size:.72rem;letter-spacing:.12em;text-transform:uppercase;opacity:.72}"
    ".stat .v{display:block;margin-top:6px;font-size:1.05rem;font-weight:800}"
    ".info-list{display:grid;gap:10px}"
    ".info{padding:13px 14px;border-radius:18px;background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1);display:flex;justify-content:space-between;gap:12px;align-items:center}"
    ".info .k{font-size:.82rem;letter-spacing:.1em;text-transform:uppercase;opacity:.72}"
    ".info .v{font-size:1rem;font-weight:800;text-align:right}"
    ".status-line{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}"
    ".chip{padding:9px 12px;border-radius:999px;background:rgba(255,255,255,.08);font-size:.85rem;font-weight:800}"
    ".focus{position:absolute;inset:0;border-radius:22px;border:2px solid transparent;pointer-events:none;transition:border-color .2s ease}"
    ".focus.left{border-color:rgba(110,232,255,.38)}"
    ".focus.right{border-color:rgba(255,216,106,.45)}"
    "@media (max-width:960px){.grid{grid-template-columns:1fr}.scene{min-height:390px}}"
    "@media (max-width:640px){.wrap{padding:12px}.card{border-radius:22px}.stats{grid-template-columns:1fr}.motor{width:154px;height:84px}.motor .bar{width:62px}.boat{height:200px}.boat-wrap{width:96%}}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='wrap'>"
    "<div class='hero'>"
    "<h1>Boat Explorer</h1>"
    "<p>Children can watch the boat move, learn which motor is stronger, and read the signal strength in one clean screen.</p>"
    "<div class='badge'><span id='connDot' class='dot'></span><span id='connText'>Connecting to boat...</span></div>"
    "</div>"
    "<div class='grid'>"
    "<section class='card'><div class='inner'>"
    "<div class='scene'>"
    "<div class='sky-glow'></div><div class='water'></div><div class='focus' id='focusRing'></div>"
    "<div class='boat-wrap'>"
    "<div class='boat-label'><span id='boatEmoji'>🛟</span><span id='boatMood'>Resting</span></div>"
    "<div class='boat' id='boat'>"
    "<div class='boat-body'></div><div class='boat-deck'></div>"
    "<div class='cabin'><div class='window left'></div><div class='window right'></div></div>"
    "<div class='smoke' id='smoke1'></div><div class='smoke second' id='smoke2'></div>"
    "<div class='motor left' id='leftMotorCard'><div class='tag'>Left motor</div><div class='power' id='leftMotorPowerText'>0%</div><div class='bar'><i id='leftMotorBar'></i></div><div class='propeller'></div></div>"
    "<div class='motor right' id='rightMotorCard'><div class='tag'>Right motor</div><div class='power' id='rightMotorPowerText'>0%</div><div class='bar'><i id='rightMotorBar'></i></div><div class='propeller'></div></div>"
    "<div class='wake' id='boatWake'></div>"
    "</div></div>"
    "<div class='hint-row'>"
    "<div class='pill'>Tap a motor to spotlight it</div>"
    "<div class='pill'>Bigger bars mean stronger push</div>"
    "<div class='pill'>Signal strength stays visible on the right</div>"
    "</div>"
    "</div></section>"
    "<section class='card'><div class='inner'>"
    "<div class='right-top'>"
    "<div class='signal-card'>"
    "<div class='signal-head'><strong>Signal strength</strong><span id='signalText'>-- dBm</span></div>"
    "<div class='meter'><i id='signalBar'></i></div>"
    "<div class='subtle' id='signalHint'>Waiting for telemetry...</div>"
    "</div>"
    "<div class='stats'>"
    "<div class='stat'><span class='k'>Packets</span><span class='v' id='packetsTxt'>0</span></div>"
    "<div class='stat'><span class='k'>Channel</span><span class='v' id='channelTxt'>--</span></div>"
    "<div class='stat'><span class='k'>Distance</span><span class='v' id='distanceTxt'>-- m</span></div>"
    "<div class='stat'><span class='k'>RTT</span><span class='v' id='rttTxt'>-- us</span></div>"
    "</div>"
    "<div class='info-list'>"
    "<div class='info'><div class='k'>Joystick</div><div class='v' id='joyTxt'>Waiting</div></div>"
    "<div class='info'><div class='k'>Drive gate</div><div class='v' id='gateTxt'>Stopped</div></div>"
    "<div class='info'><div class='k'>Boat state</div><div class='v' id='boatStateTxt'>Idle</div></div>"
    "</div>"
    "<div class='status-line'>"
    "<div class='chip' id='wifiChip'>AP: --</div>"
    "<div class='chip' id='linkChip'>Link: --</div>"
    "<div class='chip' id='rawChip'>LX/LY: -- / --</div>"
    "</div>"
    "</div></div></section>"
    "</div></div>"
    "<script>"
    "let activeSide='';"
    "const connDot=document.getElementById('connDot'),connText=document.getElementById('connText');"
    "const boat=document.getElementById('boat'),boatMood=document.getElementById('boatMood'),boatEmoji=document.getElementById('boatEmoji'),focusRing=document.getElementById('focusRing');"
    "const leftMotorCard=document.getElementById('leftMotorCard'),rightMotorCard=document.getElementById('rightMotorCard');"
    "const leftMotorPowerText=document.getElementById('leftMotorPowerText'),rightMotorPowerText=document.getElementById('rightMotorPowerText');"
    "const leftMotorBar=document.getElementById('leftMotorBar'),rightMotorBar=document.getElementById('rightMotorBar');"
    "const smoke1=document.getElementById('smoke1'),smoke2=document.getElementById('smoke2'),boatWake=document.getElementById('boatWake');"
    "const signalText=document.getElementById('signalText'),signalBar=document.getElementById('signalBar'),signalHint=document.getElementById('signalHint');"
    "const packetsTxt=document.getElementById('packetsTxt'),channelTxt=document.getElementById('channelTxt'),distanceTxt=document.getElementById('distanceTxt'),rttTxt=document.getElementById('rttTxt');"
    "const joyTxt=document.getElementById('joyTxt'),gateTxt=document.getElementById('gateTxt'),boatStateTxt=document.getElementById('boatStateTxt');"
    "const wifiChip=document.getElementById('wifiChip'),linkChip=document.getElementById('linkChip'),rawChip=document.getElementById('rawChip');"
    "function clamp(v,min,max){return Math.min(max,Math.max(min,v));}"
    "function setActiveSide(side){activeSide=side;focusRing.className='focus'+(side==='left'?' left':side==='right'?' right':'');leftMotorCard.classList.toggle('active',side==='left');rightMotorCard.classList.toggle('active',side==='right');}"
    "function signalPercentFromRssi(rssi){return clamp(((rssi+100)/60)*100,0,100);}"
    "function signalLabel(rssi){if(rssi>=-55) return 'Excellent'; if(rssi>=-67) return 'Strong'; if(rssi>=-78) return 'Okay'; return 'Weak';}"
    "function directionName(left,right){if(left===0&&right===0) return 'Drifting'; if(Math.abs(left-right)<12) return left>=0 ? 'Sailing straight' : 'Backing straight'; if(left>right) return 'Turning right'; return 'Turning left';}"
    "function moodForPower(value){if(value===0) return 'Resting'; if(value<30) return 'Little splashes'; if(value<75) return 'Zooming'; return 'Super zoom!';}"
    "function bubbleBurst(left,right){if(Math.max(Math.abs(left),Math.abs(right))<10) return; for(let i=0;i<4;i++){const bubble=document.createElement('div');bubble.className='bubble';bubble.style.left=(18+Math.random()*64)+'%';bubble.style.width=bubble.style.height=(4+Math.random()*9)+'px';bubble.style.animationDelay=(Math.random()*1.2)+'s';bubble.style.setProperty('--mx',((right-left)/3+(Math.random()*28-14))+'px');boatWake.appendChild(bubble);setTimeout(()=>bubble.remove(),4200);}}"
    "function renderStatus(d){"
    "const left=Number(d.left||d.motor1_speed||0);"
    "const right=Number(d.right||d.motor2_speed||0);"
    "const valid=!!d.link_valid;"
    "connDot.className='dot'+(valid?' on':'');"
    "connText.textContent=valid?'Boat telemetry active':'Waiting for boat...';"
    "leftMotorPowerText.textContent=Math.abs(left)+'%';rightMotorPowerText.textContent=Math.abs(right)+'%';"
    "leftMotorBar.style.width=clamp(Math.abs(left),0,127)/127*100+'%';rightMotorBar.style.width=clamp(Math.abs(right),0,127)/127*100+'%';"
    "const maxPower=Math.max(Math.abs(left),Math.abs(right));"
    "boatMood.textContent=moodForPower(maxPower);"
    "boatEmoji.textContent=maxPower===0?'🛟':(maxPower<40?'⛵':'🚀');"
    "smoke1.classList.toggle('active',maxPower>0);smoke2.classList.toggle('active',maxPower>55);"
    "boat.style.transform='translateY('+clamp(-(left+right)/45,-8,8)+'px) rotate('+clamp((right-left)/22,-8,8)+'deg)';"
    "const direction=directionName(left,right);"
    "boatStateTxt.textContent=direction;"
    "if(Math.abs(left-right)<12){setActiveSide('');}else if(left>right){setActiveSide('left');}else{setActiveSide('right');}"
    "bubbleBurst(left,right);"
    "signalText.textContent=(typeof d.rssi==='number'?d.rssi:'--')+' dBm';"
    "if(typeof d.rssi==='number'){const pct=signalPercentFromRssi(d.rssi);signalBar.style.width=pct+'%';signalHint.textContent=signalLabel(d.rssi)+' link strength';}else{signalBar.style.width='0%';signalHint.textContent='Waiting for telemetry...';}"
    "packetsTxt.textContent=d.packets||0;"
    "channelTxt.textContent='Ch '+(d.ap_channel||'--');"
    "distanceTxt.textContent=(typeof d.distance_m==='number'&&d.distance_m>=0)?d.distance_m.toFixed(1)+' m':'-- m';"
    "rttTxt.textContent=(d.rtt_us||0)+' us';"
    "joyTxt.textContent=d.joystick_connected?(d.joystick_active?'Connected, transmitting':'Connected, idle'):'No controller';"
    "gateTxt.textContent=d.joystick_active?'Transmitting':'Stopped';"
    "wifiChip.textContent='AP: '+(d.wifi_ssid||'--')+' ch'+(d.ap_channel||'--');"
    "linkChip.textContent=valid?'Link: live':'Link: down';"
    "rawChip.textContent='LX/LY: '+(typeof d.lx==='number'?d.lx:'--')+' / '+(typeof d.ly==='number'?d.ly:'--');"
    "}"
    "function refresh(){fetch('/api/status').then(r=>r.json()).then(renderStatus).catch(()=>{connDot.className='dot';connText.textContent='Connecting to boat...';});}"
    "leftMotorCard.addEventListener('click',()=>setActiveSide(activeSide==='left'?'':'left'));"
    "rightMotorCard.addEventListener('click',()=>setActiveSide(activeSide==='right'?'':'right'));"
    "refresh();setInterval(refresh,350);setInterval(()=>bubbleBurst(30,30),3500);"
    "</script>"
    "</body>"
    "</html>";
static esp_err_t http_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_api_status_handler(httpd_req_t *req)
{
    char json_buffer[512];
    snprintf(json_buffer, sizeof(json_buffer),
             "{\"motor1_speed\":%d,\"motor1_direction\":%d,\"motor2_speed\":%d,\"motor2_direction\":%d,\"left\":%d,\"right\":%d,\"packets\":%lu,\"wifi_ssid\":\"%s\",\"ap_channel\":%u,\"long_range_enabled\":%u,\"joystick_connected\":%u,\"joystick_active\":%u,\"lx\":%d,\"ly\":%d,\"rssi\":%d,\"distance_m\":%.3f,\"rtt_us\":%lu,\"link_valid\":%u}",
             current_motor_data.motor1_speed,
             current_motor_data.motor1_direction,
             current_motor_data.motor2_speed,
             current_motor_data.motor2_direction,
             current_motor_data.motor1_speed,
             current_motor_data.motor2_speed,
             (unsigned long)current_packet_count,
             current_settings.wifi_ssid,
             current_ap_channel,
             current_settings.long_range_enabled,
             current_joystick_connected,
             current_joystick_tx_enabled,
             current_joystick_lx,
             current_joystick_ly,
             current_receiver_rssi_dbm,
             (double)current_estimated_distance_m,
             (unsigned long)current_rtt_us,
             current_link_valid);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_buffer, HTTPD_RESP_USE_STRLEN);
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

int web_server_init(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = 8192;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting read-only web server on port %u", port);
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

void web_server_update_network_state(const remote_settings_t *settings, uint8_t ap_channel)
{
    web_server_update_settings(settings);
    current_ap_channel = ap_channel;
}

void web_server_update_motor_data(const motor_control_t *motor_data)
{
    if (motor_data != NULL)
    {
        memcpy(&current_motor_data, motor_data, sizeof(current_motor_data));
    }
}

void web_server_update_joystick_state(uint8_t connected, uint8_t tx_enabled, int16_t lx, int16_t ly)
{
    current_joystick_connected = connected ? 1 : 0;
    current_joystick_tx_enabled = tx_enabled ? 1 : 0;
    current_joystick_lx = lx;
    current_joystick_ly = ly;
}

void web_server_update_packet_count(uint32_t packet_count)
{
    current_packet_count = packet_count;
}

void web_server_update_link_metrics(int8_t rssi_dbm, float distance_m, uint32_t rtt_us, uint8_t valid)
{
    current_receiver_rssi_dbm = rssi_dbm;
    current_estimated_distance_m = distance_m;
    current_rtt_us = rtt_us;
    current_link_valid = valid ? 1 : 0;
}

void web_server_update_drive_command(int16_t throttle, int16_t steer, uint8_t armed)
{
    (void)throttle;
    (void)steer;
    (void)armed;
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