// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <string.h>

#include "task_wifi.hpp"

#include "task_http_client.hpp"

#include "system_registry.hpp"

#if defined (M5UNIFIED_PC_BUILD)
namespace kanplay_ns {

void task_wifi_t::task_func(task_wifi_t* me)
{
  uint8_t counter;
  for (;;) {
    ++counter;
    M5.delay(256);
    auto mode = system_registry.wifi_control.getWifiMode();
    auto op = system_registry.wifi_control.getOperation();

    auto sta_info = def::command::wifi_sta_info_t::wsi_off;
    auto ap_info = def::command::wifi_ap_info_t::wai_off;

    bool sta = false;
    bool ap = false;
    if (mode == def::command::wifi_mode_t::wifi_enable_sta) {
      sta_info = (def::command::wifi_sta_info_t)((3 & (counter >> 3))+1);
    }
    if (op == def::command::wifi_operation_t::wfop_setup_ap) {
      sta_info = def::command::wifi_sta_info_t::wsi_waiting;
      ap_info = (counter & 0x10) 
              ? def::command::wifi_ap_info_t::wai_enabled
              : def::command::wifi_ap_info_t::wai_waiting;
      system_registry.runtime_info.setWiFiStationCount((counter & 0x10) ? 1 : 0);
    }
    if (op == def::command::wifi_operation_t::wfop_setup_wps) {
      sta_info = def::command::wifi_sta_info_t::wsi_waiting;
    }
    if (op == def::command::wifi_operation_t::wfop_ota_begin) {
      system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_progress);
    }
    if (op == def::command::wifi_operation_t::wfop_ota_progress) {
      sta_info = (def::command::wifi_sta_info_t)((3 & (counter >> 3))+1);
      int progress = counter & 0x7f;
      if (progress > 101) { progress = 101; }
      system_registry.runtime_info.setWiFiOtaProgress(progress);
    }
    system_registry.runtime_info.setWiFiSTAInfo(sta_info);
    system_registry.runtime_info.setWiFiAPInfo(ap_info);
  }
}

void task_wifi_t::start(void) {
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "wifi", this);
}
};
#else
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
// #include <HttpsOTAUpdate.h>
#include <ArduinoJson.h>

#include <esp_wifi.h>
#include <esp_wps.h>
#include <esp_http_server.h>
#include <esp_http_client.h>

#include <esp_crt_bundle.h>

#if __has_include (<esp_sntp.h>)
  #include <esp_sntp.h>
  #define SNTP_ENABLED 1
#elif __has_include (<sntp.h>)
  #include <sntp.h>
  #define SNTP_ENABLED 1
#endif



// ファイルインポートマクロ
#define IMPORT_FILE(section, filename, symbol) \
extern const char symbol[], sizeof_##symbol[]; \
asm (\
  ".section " #section "\n"\
  ".balign 4\n"\
  ".global " #symbol "\n"\
  #symbol ":\n"\
  ".incbin \"" filename "\"\n"\
  ".global sizeof_" #symbol "\n"\
  ".set sizeof_" #symbol ", . - " #symbol "\n"\
  ".balign 4\n"\
  ".section \".text\"\n")

IMPORT_FILE(.rodata, "incbin/html/wifi.html", html_wifi);
IMPORT_FILE(.rodata, "incbin/html/main.html", html_main);

namespace kanplay_ns {
//-------------------------------------------------------------------------

static constexpr const char server_certificate[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n"
"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n"
"DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n"
"SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n"
"GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n"
"AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n"
"q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n"
"SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n"
"Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n"
"a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n"
"/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n"
"AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n"
"CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n"
"bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n"
"c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n"
"VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n"
"ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n"
"MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n"
"Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n"
"AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n"
"uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n"
"wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n"
"X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n"
"PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n"
"KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n"
"-----END CERTIFICATE-----";

static constexpr const char HTTP_200_html[] =
    "HTTP/1.1 200 OK\nContent-Type: text/html; "
    "charset=UTF-8\nX-Content-Type-Options: nosniff\nConnection: "
    "keep-alive\nCache-Control: no-cache\n\n"
    "<!DOCTYPE html><html lang=\"en\">";

static constexpr const char HTTP_200_json[] =
    "HTTP/1.1 200 OK\nContent-Type: application/json; "
    "charset=UTF-8\nX-Content-Type-Options: nosniff\nConnection: "
    "keep-alive\nCache-Control: no-cache\n\n";

static constexpr const char HTML_footer[] =
    // "<div class='ft'>Copyright &copy;2025 Instachord</div>"
    "</div>\n</body></html>\n\n";

static constexpr const char HTML_style[] =
    "<style>"
    "html,body{margin:0;padding:0;font-family:sans-serif;background-color:#f5f5f5}"
    ".ct{min-height:100%;width:85%;margin:0 auto;display:flex;flex-direction: column;font-size:5vw}"
    "h1{display:block;margin:0;padding:3vw 0;font-size:8vw}"
    "h2{margin:0;padding:2vw 3vw;border-radius:2vw 2vw 0 0;font-size:6vw;background-color:#909ba1}"
    "h1,.ft{text-align:center}"
    ".ft{padding:10px 0;font-size:4vw}"
    ".main{flex-grow:1}"
    ".ls{border-radius:2vw;background-color:#bfced6}"
    "a{padding:3vw;display:block;color:#000;border-bottom:1px solid #eee;text-decoration:none}"
    "a.active,a:hover{color:#fff;background-color:#8b2de2}"
    "a:last-child:hover{border-radius:0 0 2vw 2vw}"
    "form {margin:0}"
    ".fg{margin:10px 0;padding:5px}"
    ".fg input{margin-top:5px;padding:5px 10px;width:100%;border:1px solid #000;outline:none;border-radius:2vw;font-size:6vw}"
    ".fg select{margin-top:5px;padding:5px 10px;width:100%;border:1px solid #000;outline:none;border-radius:2vw;font-size:6vw}"
    ".fc{padding-left:2vw}"
    ".fc input[type=\"checkbox\"]{width:5vw;height:5vw;vertical-align:middle}"
    ".fc button{margin:10px 0 0 0;padding:10px;width:100%;font-size:8vw;border:none;border-radius:2vw;background-color:#3aee70;outline:none;cursor:pointer}"
    // "@media screen and (min-width:720px){"
    // ".ct{width:50%;max-width:720px}"
    // "h1{padding:20px 0;font-size: 38px;}"
    // ".ft{font-size:18px;}"
    // ".ct,.ls a,.fg input,.fc button{font-size:24px;}"
    // ".fc{padding-left:5px;}"
    // ".fc input[type=\"checkbox\"]{left:10px;top:0px;width:20px;height:20px;}"
    // ".ls,.fc button,.fg input{border-radius:10px;}"
    // "h2{font-size:32px;padding:10px;border-radius:10px 10px 0 0;}"
    // "a{padding:10px;}"
    // "a:last-child:hover{border-radius:0 0 10px 10px;}"
    // "}"
    "</style>";


#if 0 // __has_include (<ESPAsyncWebServer.h>)
static void response_404(AsyncWebServerRequest *request) {
  request->send(404, "text/html",
    "<!DOCTYPE html><html lang=\"en\"><head><meta http-equiv='Content-Type' content='text/html; charset=utf-8'>"
    "<title>404</title><script>window.onload=function(){setTimeout(\"location.href='/'\",999);}</script>"
    "</head><body>404 Page not found.<br></body></html>"
  );
}

static void response_getssid(AsyncWebServerRequest *request) {
  int count;
  int retry = 256;
  while (0 > (count = WiFi.scanComplete()) && --retry) {
    if (count == -2) {
      WiFi.scanNetworks(true);
    }
    M5.delay(16);
  }

  String res = "{\"ssids\":[\"";
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      auto ssid = WiFi.SSID(i);
      if (i) res += "\",\"";
      // snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\"}", ssid.c_str());
      res += ssid;
    }
  }
  res += "\"]}";
  WiFi.scanDelete();

  request->send(200, "application/json", res);
}

static void response_ctrl(AsyncWebServerRequest *request) {
  struct btnctrl_t {
    def::command::command_param_t command;
    const char* keycode_array;
  };
  using namespace def::command;
  static constexpr const btnctrl_t btnctrl_table[] = {
    {{internal_button,11}, "qQ7"}, {{internal_button,12}, "wW8"}, {{internal_button,13}, "eE9"}, {{internal_button,14}, "rR" }, {{internal_button,15}, "tT"},
    {{internal_button, 6}, "aA4"}, {{internal_button, 7}, "sS5"}, {{internal_button, 8}, "dD6"}, {{internal_button, 9}, "fF" }, {{internal_button,10}, "gG"},
    {{internal_button, 1}, "zZ1"}, {{internal_button, 2}, "xX2"}, {{internal_button, 3}, "cC3"}, {{internal_button, 4}, "vV0"}, {{internal_button, 5}, "bB"},
  };

  String res = 
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">\n"
    "<script>\n"
    // "const ws=new WebSocket(`ws://${window.location.hostname}/ws`);"
    "const ws=new WebSocket('/ws');"
    "let ct={";

  static constexpr const size_t linebuf_len = 256;
  char linebuf[linebuf_len];

  for (auto& btn : btnctrl_table) {
    int i = 0;
    while (btn.keycode_array[i]) {
      snprintf(linebuf, linebuf_len, "'%c':%d,", btn.keycode_array[i], btn.command.raw);
      res += linebuf;
      ++i;
    }
    res += linebuf;
  }
  res +=
    "};\n"
    "document.addEventListener('keydown',function(e){"
      "if(!e.repeat&&e.key in ct){"
        "ws.send('cmd=p'+ct[e.key]);"
      "}"
    "});\n"
    "document.addEventListener('keyup',function(e){"
      "if(!e.repeat&&e.key in ct){"
        "ws.send('cmd=r'+ct[e.key]);"
      "}"
    "});\n"
    "</script>\n"
    "</head><body>\n";


  auto current_slot = system_registry.current_slot;
  for (int part = 0; part < def::app::max_chord_part; ++part) {
    snprintf(linebuf, linebuf_len, "<h3>Part %d</h3><table style='border:1px solid black;'>", part + 1);
    res += linebuf;
    auto chord_part = &(current_slot->chord_part[part]);
    for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
      res += "<tr>";
      for (int step = 0; step < def::app::max_arpeggio_step; ++step) {
        // client->printf("<td>%d</td>", chord_part->arpeggio.getVelocity(step, pitch));
        int v = chord_part->arpeggio.getVelocity(step, pitch);
        if (v) {
          snprintf(linebuf, linebuf_len, "<td>%d</td>", v);
          res += linebuf;
        } else {
          res += "<td>___</td>";
        }
      }
      res += "</tr>";
    }
    res += "</table>";
  }
  res += HTML_footer;
  request->send(200, "text/html", res);
}

static void response_wifi(AsyncWebServerRequest *request) {
  // APモードでなければ wifi設定を使用できないようにする
   auto operation = system_registry.wifi_control.getOperation();
  if (operation != def::command::wifi_operation_t::wfop_setup_ap) {
    request->redirect("/");
    return;
  }
  int params = request->params();
  if (params) {
    std::string ssid, password;
    for (int i = 0; i < params; ++i) {
      auto param = request->getParam(i);
      if (param->isPost()) {
        if (param->name() == "s") {
          ssid = param->value().c_str();
        } else if (param->name() == "p") {
          password = param->value().c_str();
        }
      }
    }
    if (ssid.length()) {
      WiFi.begin(ssid.c_str(), password.c_str());
      system_registry.wifi_control.setMode(def::command::wifi_mode_t::wifi_enable_sta);
      system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
      // M5_LOGD("ssid : %s  password : %s", ssid.c_str(), password.c_str());
      request->redirect("/");
    }
    return;
  }

static constexpr const char html[] = R"(
<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'><style>
html,body{margin:0;padding:0;font-family:sans-serif;background-color:#f5f5f5}
.ct{min-height:100%;width:85%;margin:0 auto;display:flex;flex-direction: column;font-size:5vw}
h1{display:block;margin:0;padding:3vw 0;font-size:8vw}
h2{margin:0;padding:2vw 3vw;border-radius:2vw 2vw 0 0;font-size:6vw;background-color:#909ba1}
h1,.ft{text-align:center}
.ft{padding:10px 0;font-size:4vw}
.main{flex-grow:1}
.ls{border-radius:2vw;background-color:#bfced6}
a{padding:3vw;display:block;color:#000;border-bottom:1px solid #eee;text-decoration:none}
a.active,a:hover{color:#fff;background-color:#8b2de2}
a:last-child:hover{border-radius:0 0 2vw 2vw}
form {margin:0; position:relative;}
.fg{margin:10px 0;padding:5px}
.fg input{margin-top:5px;padding:5px 10px;width:100%;border:1px solid #000;outline:none;border-radius:2vw;font-size:6vw}
.fg select{margin-top:5px;padding:5px 10px;width:100%;border:1px solid #000;outline:none;border-radius:2vw;font-size:6vw}
#dd {display:none;margin-top:5px;padding:5px 10px;width:100%;border:1px solid #000;outline:none;border-radius:2vw;font-size:6vw}
.fc{padding-left:2vw}
.fc input[type="checkbox"]{width:5vw;height:5vw;vertical-align:middle}
.fc button{margin:10px 0 0 0;padding:10px;width:100%;font-size:8vw;border:none;border-radius:2vw;background-color:#3aee70;outline:none;cursor:pointer}
#dd div { padding: 8px; cursor: pointer; }
#dd div:hover { background-color: #f1f1f1; }
</style></head>
<body></head><body><div class='ct'><h1>KantanPlayCore</h1><h2>WiFi setup</h2>
<div class='main'><form method='POST' action='wifi' autocomplete='off'>
<div class='fg'><label for='s'>SSID: </label><input name='s' id='s' placeholder='input or select' onfocus='showdd()' oninput='fltdd()'></div>
<div id='dd'></div>
<div class='fg'><label for='p'>Password: </label><input name='p' id='p' maxlength='64' type='password' placeholder='Password'></div>
<div class='fc'><input id='show_pwd' type='checkbox' onclick='h()'><label for='show_pwd'>Show Password</label>
<button type='submit'>Save</button></div>
</form></div></div></body>
<script>
let ssids = [];
async function getssid() {
const res = await fetch('/getssid');
const data = await res.json();
ssids = data.ssids;}
function h() { var p=document.getElementById('p');p.type==='text'?p.type='password':p.type='text';}
function showdd() {
const dd = document.getElementById('dd');
dd.innerHTML = ssids.map(ssid => '<div onclick="selopt(\'' + ssid + '\') ">' + ssid + '</div>').join('');
dd.style.display = 'block';}
function fltdd(){const input=document.getElementById('s').value.toLowerCase();
const dd=document.getElementById('dd');
dd.innerHTML=ssids.filter(ssid => ssid.toLowerCase().includes(input)).map(ssid=>'<div onclick="selopt(\'' + ssid + '\') ">' + ssid + '</div>').join('');
dd.style.display=dd.innerHTML?'block':'none';}
function selopt(value) {document.getElementById('s').value=value;document.getElementById('dd').style.display='none';}
document.addEventListener('click',e=>{if(e.target.id!=='s')document.getElementById('dd').style.display='none';});
document.addEventListener('DOMContentLoaded',getssid);
</script></html>)";
  request->send(200, "text/html", html);
}

static void response_main(AsyncWebServerRequest *request) {
  static constexpr const char html_1[] =
      "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">\n"
      "<meta name=\"viewport\" content=\"width=device-width, "
      "initial-scale=1.0\">\n"
      "<title>KantanPlayCore Top Menu</title>\n";

  static constexpr const char html_2[] =
      "</head><body><div class='ct'><h1>KantanPlayCore Top menu</h1>"
      "<div class='main'><div class='ls'><h2>LAN</h2>";

  static constexpr const char html_3[] =
      "<a href=\"/ctrl\">Browser control</a>\n"
      "</div></div>\n";

  String res = html_1;
  res += HTML_style;
  res += html_2;

  auto operation = system_registry.wifi_control.getOperation();
  if (operation == def::command::wifi_operation_t::wfop_setup_ap) {
      res += "<a href=\"/wifi\">WiFi setting</a>\n";
  }

  res += html_3;
  res += HTML_footer;

  request->send(200, "text/html", res);
  return;
}

static void response_top(AsyncWebServerRequest *request) {
  auto operation = system_registry.wifi_control.getOperation();
  request->redirect(
    (operation == def::command::wifi_operation_t::wfop_setup_ap)
    ? "/wifi"
    : "/main"
  );
}
static AsyncWebServer webServer(http_port);
static AsyncWebSocket webSocket("/ws");

static void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  default:
    break;
  case WS_EVT_CONNECT:
    M5_LOGV("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    M5_LOGV("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
//    handleWebSocketMessage(arg, data, len);
    if (len) {
      if (memcmp(data, "cmd=", 4) == 0) {
        bool press = (data[4] == 'p');
        def::command::command_param_t cmd;
        data[len] = 0;
        cmd.raw = atoi((const char*)&data[5]);
        system_registry.operator_command.addQueue(cmd, press);
      }
    }

    break;
  }
  // クライアントの接続、切断、データ受信を処理
}
#else

static esp_err_t response_redirect(httpd_req_t *req, const char *location)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t response_top_handler(httpd_req_t *req)
{
  auto operation = system_registry.wifi_control.getOperation();
  if (operation == def::command::wifi_operation_t::wfop_setup_ap) {
    return response_redirect(req, "/wifi");
  }
  return response_redirect(req, "/main");
}

static esp_err_t response_wifi_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_wifi, (uint32_t)sizeof_html_wifi);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

static esp_err_t response_main_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_main, (uint32_t)sizeof_html_main);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

static esp_err_t response_ctrl_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");

  struct btnctrl_t {
    def::command::command_param_t command;
    const char* keycode_array;
  };
  using namespace def::command;
  static constexpr const btnctrl_t btnctrl_table[] = {
    {{internal_button,11}, "qQ7"}, {{internal_button,12}, "wW8"}, {{internal_button,13}, "eE9"}, {{internal_button,14}, "rR" }, {{internal_button,15}, "tT"},
    {{internal_button, 6}, "aA4"}, {{internal_button, 7}, "sS5"}, {{internal_button, 8}, "dD6"}, {{internal_button, 9}, "fF" }, {{internal_button,10}, "gG"},
    {{internal_button, 1}, "zZ1"}, {{internal_button, 2}, "xX2"}, {{internal_button, 3}, "cC3"}, {{internal_button, 4}, "vV0"}, {{internal_button, 5}, "bB"},
  };

  httpd_resp_sendstr_chunk(req, 
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">\n"
    "<script>\n"
    "let ct={"
  );

  char linebuf[16];
  for (auto& btn : btnctrl_table) {
    int i = 0;
    do {
      snprintf(linebuf, sizeof(linebuf), "'%c':%d,", btn.keycode_array[i], btn.command.raw);
      httpd_resp_sendstr_chunk(req, linebuf);
    } while (btn.keycode_array[++i]);
    httpd_resp_sendstr_chunk(req, "\n");
  }
  httpd_resp_sendstr_chunk(req, 
    "};\n"
    "const ws=new WebSocket('/ws');"
    "document.addEventListener('keydown',function(e){ if(!e.repeat&&e.key in ct){ ws.send('cmd=p'+ct[e.key]); } });\n"
    "document.addEventListener('keyup',function(e){ if(!e.repeat&&e.key in ct){ ws.send('cmd=r'+ct[e.key]); } });\n"
    "</script>\n"
    "</head><body>KEYBOARD CONTROL</body></html>\n"
  );
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

static esp_err_t response_ssid_handler(httpd_req_t *req) {
  int count;
  int retry = 256;
  while (0 > (count = WiFi.scanComplete()) && --retry) {
    if (count == -2) {
      WiFi.scanNetworks(true);
    }
    M5.delay(16);
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr_chunk(req, "{\"ssids\":[\"");
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      auto ssid = WiFi.SSID(i);
      if (i) {
        httpd_resp_sendstr_chunk(req, "\",\"");
      }
      httpd_resp_sendstr_chunk(req, ssid.c_str());
    }
  }
  WiFi.scanDelete();
  httpd_resp_sendstr_chunk(req, "\"]}");
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

static std::string url_decode(const std::string& str) {
  std::string decoded;
  for (int i = 0; i < str.length(); i++) {
    char ch = str[i];
    if (ch == '%') {
      int ii;
      sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
      ch = static_cast<char>(ii);
      i += 2;
    } else if (ch == '+') {
      ch = ' ';
    }
    decoded += ch;
  }
  return decoded;
}

static esp_err_t response_post_wifi_handler(httpd_req_t *req) {
  int ret, len = req->content_len;
  std::string ssid, password;
  esp_err_t res;
  {  
    std::vector<char> res_buf (len+1, 0);
    if ((ret = httpd_req_recv(req, res_buf.data(), len)) <= 0) {
      return ESP_FAIL;
    }
    std::vector<char> buf (len+1, 0);
    memset(buf.data(), 0, buf.size());
    res = httpd_query_key_value(res_buf.data(), "s", buf.data(), buf.size());
    if (res == ESP_OK) {
      ssid = url_decode(buf.data());
      memset(buf.data(), 0, buf.size());
      res = httpd_query_key_value(res_buf.data(), "p", buf.data(), buf.size());
      if (res == ESP_OK) {
        password = url_decode(buf.data());
      }
    }
  }

  // M5_LOGD("ssid : %s  password : %s", ssid, password);

  if (res == ESP_OK) {
    WiFi.begin(ssid.c_str(), password.c_str());
    system_registry.wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_enable_sta);
    system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    return response_redirect(req, "/");
  }
  return ESP_OK;
}


#if 0
/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    static const char * data = "Async data";
    auto resp_arg = (async_resp_arg*)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    auto resp_arg = (async_resp_arg*)malloc(sizeof(async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}
#endif
static esp_err_t response_ws_handler(httpd_req_t *req)
{
  if (req->method == HTTP_GET) {
    // M5_LOGI("Handshake done, the new connection was opened");
    return ESP_OK;
  }
  httpd_ws_frame_t ws_pkt;
  uint8_t *buf = nullptr;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    // M5_LOGE("httpd_ws_recv_frame failed to get frame len with %d", ret);
    return ret;
  }
  // M5_LOGI("frame len is %d", ws_pkt.len);
  if (ws_pkt.len) {
    /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
    buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
    if (buf == nullptr) {
      // M5_LOGE("Failed to calloc memory for buf");
      return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    /* Set max_len = ws_pkt.len to get the frame payload */
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      // M5_LOGE("httpd_ws_recv_frame failed with %d", ret);
      free(buf);
      return ret;
    }
    // M5_LOGI("Got packet with message: %s", ws_pkt.payload);
    // auto data = ws_pkt.payload;
    if (memcmp(buf, "cmd=", 4) == 0) {
      bool press = (buf[4] == 'p');
      def::command::command_param_t cmd;
      // buf[ws_pkt.len] = 0;
      cmd.raw = atoi((const char*)&buf[5]);
      system_registry.operator_command.addQueue(cmd, press);
    }
  }
/*
  M5_LOGI("Packet type: %d", ws_pkt.type);
  if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
    strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
    free(buf);
    return trigger_async_send(req->handle, req);
  }
  ret = httpd_ws_send_frame(req, &ws_pkt);
  if (ret != ESP_OK) {
    M5_LOGE("httpd_ws_send_frame failed with %d", ret);
  }
*/
  free(buf);
  return ret;
}

static constexpr const httpd_uri uri_table[] = {
  { "/"    , HTTP_GET , response_top_handler      , nullptr, false, false, nullptr },
  { "/wifi", HTTP_GET , response_wifi_handler     , nullptr, false, false, nullptr },
  { "/main", HTTP_GET , response_main_handler     , nullptr, false, false, nullptr },
  { "/ctrl", HTTP_GET , response_ctrl_handler     , nullptr, false, false, nullptr },
  { "/ssid", HTTP_GET , response_ssid_handler     , nullptr, false, false, nullptr },
  { "/wifi", HTTP_POST, response_post_wifi_handler, nullptr, false, false, nullptr },
  { "/ws"  , HTTP_GET , response_ws_handler       , nullptr,  true, false, nullptr },
};

static httpd_handle_t start_webserver(void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  // Start the httpd server
  M5_LOGI("Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    // Registering the ws handler
    M5_LOGI("Registering URI handlers");

    for (auto& uri : uri_table) {
      httpd_register_uri_handler(server, &uri);
    }

    return server;
  }

  M5_LOGI("Error starting server!");
  return nullptr;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
  if (server) {
    // Stop the httpd server
    for (auto& uri : uri_table) {
      httpd_unregister_uri(server, uri.uri);
    }
    return httpd_stop(server);
  }
  return ESP_OK;
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
  httpd_handle_t* server = (httpd_handle_t*) arg;
  if (*server) {
    M5_LOGI("Stopping webserver");
    if (stop_webserver(*server) == ESP_OK) {
      *server = nullptr;
    } else {
      M5_LOGE("Failed to stop http server");
    }
  }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        M5_LOGI("Starting webserver");
        *server = start_webserver();
    }
}

static httpd_handle_t http_server = NULL;

#endif

static constexpr const size_t http_port = 80;
static constexpr const size_t dns_port = 53;

static DNSServer dnsServer;
static TaskHandle_t _wifi_task_handle = nullptr;
static TaskHandle_t _wifi_info_task_handle = nullptr;

static bool wps_enabled = false;

static bool wpsStart() {
  // esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
  esp_wps_config_t config;
  config.wps_type = WPS_TYPE_PBC;
  strncpy(config.factory_info.manufacturer, "ESPRESSIF", sizeof(config.factory_info.manufacturer) - 1);
  strncpy(config.factory_info.model_number, CONFIG_IDF_TARGET, sizeof(config.factory_info.model_number) - 1);
  strncpy(config.factory_info.model_name, "ESPRESSIF IOT", sizeof(config.factory_info.model_name) - 1);
  strncpy(config.factory_info.device_name, "ESP DEVICE", sizeof(config.factory_info.device_name) - 1);
  strncpy(config.pin, "00000000", sizeof(config.pin) - 1);

  esp_err_t err = esp_wifi_wps_enable(&config);
  if (err != ESP_OK) {
    M5_LOGE("WPS Enable Failed: 0x%x: %s", err, esp_err_to_name(err));
    return false;
  }
  err = esp_wifi_wps_start(0);
  if (err != ESP_OK) {
    M5_LOGE("WPS Start Failed: 0x%x: %s", err, esp_err_to_name(err));
    return false;
  }
  wps_enabled = true;
  return true;
}

static void wpsStop() {
  wps_enabled = false;
  esp_err_t err = esp_wifi_wps_disable();
  if (err != ESP_OK) {
    M5_LOGE("WPS Disable Failed: 0x%x: %s", err, esp_err_to_name(err));
  }
}

static void task_wifi_info(void*) {
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, def::ntp::server1);
    esp_sntp_setservername(1, def::ntp::server2);
    esp_sntp_setservername(2, def::ntp::server3);
    esp_sntp_init();
//  */
  bool ntp_sync = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, 1000);
    {
      auto wifi_status = WiFi.STA.status();
      def::command::wifi_sta_info_t wifi_sta_info = def::command::wifi_sta_info_t::wsi_error;
      if (wifi_status == WL_CONNECTED) {
        int rssi = WiFi.STA.RSSI();
        rssi = ((rssi + 127) >> 5);
        rssi += 1;
        if (rssi < 0) rssi = 0;
        wifi_sta_info = (def::command::wifi_sta_info_t)rssi;

        if (!ntp_sync) {
          if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            ntp_sync = true;
            system_registry.runtime_info.setSntpSync(true);
          }
          // printf("sntp: run\n");
        }
      } else {
        switch (wifi_status) {
        case WL_STOPPED:
          wifi_sta_info = def::command::wifi_sta_info_t::wsi_off;
          break;
        case WL_IDLE_STATUS:
        case WL_SCAN_COMPLETED:
        case WL_DISCONNECTED:
          wifi_sta_info = def::command::wifi_sta_info_t::wsi_waiting;
          break;

        default:
        case WL_NO_SHIELD:
        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
          // wifi_sta_info = def::command::wifi_sta_info_t::wsi_error;
          break;
        }
      }
      system_registry.runtime_info.setWiFiSTAInfo(wifi_sta_info);
    }
    {
      def::command::wifi_ap_info_t wifi_ap_info = def::command::wifi_ap_info_t::wai_off;
      if (WiFi.AP.started()) {
        if (WiFi.AP.connected()) {
          wifi_ap_info = def::command::wifi_ap_info_t::wai_enabled;
        } else {
          wifi_ap_info = def::command::wifi_ap_info_t::wai_waiting;
        }
        system_registry.runtime_info.setWiFiStationCount(WiFi.AP.stationCount());
      }
      system_registry.runtime_info.setWiFiAPInfo(wifi_ap_info);
    }
  }
}

static void wifiEvent(WiFiEvent_t event) {
  switch (event) {
  // case ARDUINO_EVENT_WIFI_STA_START:
  //   M5_LOGV("Station Mode Started");
  //   break;
  // case ARDUINO_EVENT_WIFI_STA_GOT_IP:
  //   M5_LOGV("Connected to : %s", WiFi.SSID().c_str());
  //   M5_LOGV("Got IP: %s", WiFi.localIP().toString().c_str());
  //   break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    // if (system_registry.wifi_control.getMode() == def::command::wifi_mode_t::wifi_enable_sta) {
    //   M5_LOGV("Disconnected from station, attempting reconnection");
    //   WiFi.reconnect();
    // }
    break;
  case ARDUINO_EVENT_WPS_ER_SUCCESS:
    M5_LOGV("WPS Successful, stopping WPS and connecting to: %s", WiFi.SSID().c_str());
    wpsStop();
    system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry.wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_enable_sta);
    break;
  case ARDUINO_EVENT_WPS_ER_FAILED:
  case ARDUINO_EVENT_WPS_ER_TIMEOUT:
    wpsStop();
    if (system_registry.wifi_control.getOperation() == def::command::wifi_operation_t::wfop_setup_wps) {
      wpsStart();
    }
    break;
  default:
    break;
  }

  BaseType_t xHigherPriorityTaskWoken;
  xTaskNotifyFromISR(_wifi_info_task_handle, event, eNotifyAction::eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void task_wifi_t::start(void)
{
#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "wifi", this);
#else

  xTaskCreatePinnedToCore(task_wifi_info, "wi", 2048, this, 0, &_wifi_info_task_handle, def::system::task_cpu_wifi);

  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "wifi", 4096, this, def::system::task_priority_wifi, &_wifi_task_handle, def::system::task_cpu_wifi);
  system_registry.wifi_control.setNotifyTaskHandle(_wifi_task_handle);
  WiFi.onEvent(wifiEvent);

#if 0 // __has_include (<ESPAsyncWebServer.h>)
  webSocket.onEvent(webSocketEvent);
  webServer.addHandler(&webSocket);
  webServer.on("/"       , response_top    );
  webServer.on("/wifi"   , response_wifi   );
  webServer.on("/main"   , response_main   );
  webServer.on("/ctrl"   , response_ctrl   );
  webServer.on("/getssid", response_getssid);
  webServer.onNotFound(response_404);
#else

  // esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &http_server);
  // esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &http_server);

#endif


#endif
}

void task_wifi_t::task_func(task_wifi_t* me)
{
  task_http_client_t task_http_client;

  struct control_flg_t {
    union {
      struct {
        uint8_t ap : 1;
        uint8_t sta : 1;
        uint8_t server : 1;
        uint8_t scan : 1;
        uint8_t wps : 1;
      };
      uint8_t raw = 0;
    };
    void set(def::command::wifi_mode_t m, def::command::wifi_operation_t o, def::command::webserver_mode_t s) {
      raw = 0;
      switch (m) {
      default:
      case def::command::wifi_mode_t::wifi_disable:
        break;
      case def::command::wifi_mode_t::wifi_enable_sta:
        sta = 1;
        break;
      case def::command::wifi_mode_t::wifi_enable_ap:
        ap = 1;
        break;
      case def::command::wifi_mode_t::wifi_enable_sta_ap:
        ap = 1;
        sta = 1;
        break;
      }

      switch (o) {
      default:
      case def::command::wifi_operation_t::wfop_disable:
        break;
      case def::command::wifi_operation_t::wfop_setup_ap:
        ap = 1;
        scan = 1;
        server = 1;
        break;
      case def::command::wifi_operation_t::wfop_setup_wps:
        wps = 1;
        break;
      case def::command::wifi_operation_t::wfop_ota_begin:
      case def::command::wifi_operation_t::wfop_ota_progress:
        sta = 1;
        break;
      }
    
      switch (s) {
      default: break;
      case def::command::webserver_mode_t::ws_enable:
        ap = 1;
        sta = 1;
        server = 1;
        break;
      }
    }
  };
  control_flg_t ctrl_flg;

  for (;;) {
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(2048);
#else

    int wait = 1024;
    if (ctrl_flg.ap || ctrl_flg.server) {
      dnsServer.processNextRequest();
      wait = 4;
    }
    taskYIELD();
    ulTaskNotifyTake(pdTRUE, wait);

    auto mode = system_registry.wifi_control.getWifiMode();
    auto op = system_registry.wifi_control.getOperation();
    auto webserver_mode = system_registry.wifi_control.getWebServerMode();
    control_flg_t prev;
    prev.raw = ctrl_flg.raw;
    ctrl_flg.set(mode, op, webserver_mode);
    if (prev.raw != ctrl_flg.raw) {
// M5_LOGI("wifi_ctrl: %d", mode);
      if (!ctrl_flg.wps && wps_enabled) {
        wpsStop();
      }
      if (prev.server && !ctrl_flg.server) {
#if 0 // __has_include (<ESPAsyncWebServer.h>)
        webServer.end();
#else
        if (http_server) {
          stop_webserver(http_server);
          http_server = nullptr;
        }
#endif
        MDNS.end();
      }
      if (prev.scan && !ctrl_flg.scan) {
        WiFi.scanDelete();
      }
      if (prev.ap != ctrl_flg.ap || prev.sta != ctrl_flg.sta || prev.wps != ctrl_flg.wps) {
        if (prev.sta && !ctrl_flg.sta) {
          WiFi.STA.disconnect();
        }
        if (prev.ap && !ctrl_flg.ap) {
          dnsServer.stop();
          WiFi.AP.end();
        }
        M5.delay(16);
        if (!ctrl_flg.sta && !ctrl_flg.ap && !ctrl_flg.wps) {
// esp_netif_deinit();
          WiFi.disconnect(true);
          WiFi.mode(WIFI_MODE_NAN);
          system_registry.task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_WIFI);
        } else {
          system_registry.task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_WIFI);

// esp_netif_init();
// wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
// ESP_ERROR_CHECK(esp_wifi_init(&cfg));
// esp_netif_sntp_init();

          if (ctrl_flg.ap) {
            WiFi.mode(ctrl_flg.sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
            WiFi.softAP(def::app::wifi_ap_ssid, def::app::wifi_ap_pass);
          } else {
            WiFi.mode(WIFI_MODE_STA);
          }
          if (ctrl_flg.wps && !wps_enabled) {
            M5.delay(16);
            wpsStart();
          }
          if (ctrl_flg.sta) {
            M5.delay(16);
            WiFi.begin();
          }
        }
      }
      if (!prev.scan && ctrl_flg.scan) {
        M5.delay(16);
        WiFi.scanNetworks(true);
      }
      if (ctrl_flg.server && !prev.server) {
        M5.delay(16);
#if 0 // __has_include (<ESPAsyncWebServer.h>)
        webServer.begin();
#else
        http_server = start_webserver();
#endif
        MDNS.begin(def::app::wifi_mdns);
        MDNS.addService("http", "tcp", http_port);
        if (ctrl_flg.ap) {
          dnsServer.start( dns_port, "*", WiFi.softAPIP() );
        }
      }
    }
    if (op == def::command::wifi_operation_t::wfop_ota_begin) {
      system_registry.runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connecting);

      if (WiFi.status() == WL_CONNECTED) {
        system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_progress);
        task_http_client.exec_ota(def::app::url_ota_info);
      }
    }

#endif
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
