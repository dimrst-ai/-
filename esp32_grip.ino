/*
  Управление трёхпалым самоцентрирующимся захватом — версия для ЖЕЛЕЗА (ESP32).
  ESP32 поднимает свою Wi-Fi сеть и работает как сервер.
  Состояние хранится на ESP32. Те же эндпоинты, что и в server.py.

  Двигатель: шаговый NEMA 17 через драйвер (A4988 / DRV8825 — STEP/DIR).

  Подключение к сети с телефона/ноутбука:
    Сеть:   Grip3Control
    Пароль: 12345678
    Адрес:  192.168.4.1

  Прошивается через Arduino IDE (плата "ESP32 Dev Module").
  Нужна библиотека: AccelStepper (Менеджер библиотек -> найти "AccelStepper").
*/

#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <SPIFFS.h>

// Файл во внутренней flash-памяти ESP32, куда пишется состояние
#define STATE_FILE "/state.json"

// ---- Wi-Fi точка доступа ----
const char* AP_SSID = "Grip3Control";
const char* AP_PASS = "12345678";

// ---- Пины драйвера шагового двигателя ----
#define STEP_PIN 26
#define DIR_PIN  27
#define EN_PIN   25   // enable драйвера (LOW = включён у большинства драйверов)

// ---- Параметры механики ----
const int   MIN_MM = 10;        // зазор при полном сжатии
const int   MAX_MM = 80;        // зазор при полном раскрытии
const long  STEPS_FULL = 2000;  // шагов от полного сжатия до полного раскрытия
                                // (подбирается под твою передачу и микрошаг драйвера)

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
WebServer server(80);

// Прототипы функций (чтобы порядок определения не влиял на компиляцию)
void saveState();
void loadState();
String stateJson();
int percentToMm(int p);
long percentToSteps(int p);

// ---- Состояние, которое хранит СЕРВЕР (ESP32) ----
int  g_percent = 100;     // 0 = сжато, 100 = раскрыто
int  g_gap_mm  = MAX_MM;
bool g_moving  = false;
String g_status = "open";

long percentToSteps(int p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  return (long)(STEPS_FULL * p / 100.0);
}

int percentToMm(int p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  return (int)(MIN_MM + (MAX_MM - MIN_MM) * p / 100.0);
}

void applyPercent(int p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  g_percent = p;
  g_gap_mm  = percentToMm(p);
  g_moving  = true;                 // двигатель пошёл
  digitalWrite(EN_PIN, LOW);        // включить драйвер
  stepper.moveTo(percentToSteps(p));
  if (p <= 0)      g_status = "closed";
  else if (p >= 100) g_status = "open";
  else             g_status = "partial";
  saveState();   // записываем состояние в файл во flash-памяти
}

String stateJson() {
  String s = "{";
  s += "\"percent\":" + String(g_percent) + ",";
  s += "\"gap_mm\":" + String(g_gap_mm) + ",";
  s += "\"moving\":" + String(g_moving ? "true" : "false") + ",";
  s += "\"status\":\"" + g_status + "\"";
  s += "}";
  return s;
}

// Записать текущее состояние в файл во flash-памяти
void saveState() {
  File f = SPIFFS.open(STATE_FILE, "w");
  if (f) { f.print(stateJson()); f.close(); }
}

// Прочитать сохранённый percent при запуске (простой разбор)
void loadState() {
  if (!SPIFFS.exists(STATE_FILE)) return;
  File f = SPIFFS.open(STATE_FILE, "r");
  if (!f) return;
  String s = f.readString();
  f.close();
  int i = s.indexOf("\"percent\":");
  if (i >= 0) {
    int p = s.substring(i + 10).toInt();
    g_percent = p;
    g_gap_mm = percentToMm(p);
    if (p <= 0) g_status = "closed";
    else if (p >= 100) g_status = "open";
    else g_status = "partial";
    stepper.setCurrentPosition(percentToSteps(p));
  }
}

void sendState() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", stateJson());
}

// ---- HTML панель (отдаётся самим ESP32) ----
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Управление захватом</title></head><body>
<h1>Трёхпалый самоцентрирующийся захват</h1>
<p>Управление через сервер. Состояние хранится на ESP32.</p>
<h2>Команды</h2>
<button onclick="cmd('/grip')">Сжать</button>
<button onclick="cmd('/release')">Разжать</button>
<button onclick="cmd('/stop')">Стоп</button>
<h2>Раскрытие (%)</h2>
<input type="range" id="slider" min="0" max="100" value="100"
 oninput="document.getElementById('sv').textContent=this.value">
<span id="sv">100</span>%<br><br>
<button onclick="setP(document.getElementById('slider').value)">Применить</button>
<h2>Статус</h2>
<p>Раскрытие: <b id="percent">—</b> %</p>
<p>Зазор между губками: <b id="gap">—</b> мм</p>
<p>Состояние: <b id="status">—</b></p>
<p>Двигатель крутится: <b id="moving">—</b></p>
<script>
function cmd(u){fetch(u).then(r=>r.json()).then(show);}
function setP(p){fetch('/set?percent='+p).then(r=>r.json()).then(show);}
function show(s){
 document.getElementById('percent').textContent=s.percent;
 document.getElementById('gap').textContent=s.gap_mm;
 document.getElementById('status').textContent=s.status;
 document.getElementById('moving').textContent=s.moving?'да':'нет';}
function poll(){fetch('/status').then(r=>r.json()).then(show);}
setInterval(poll,1000);poll();
</script></body></html>
)HTML";

void handleRoot()    { server.send_P(200, "text/html", PAGE); }
void handleGrip()    { applyPercent(0);   sendState(); }
void handleRelease() { applyPercent(100); sendState(); }
void handleStop()    { stepper.stop(); g_moving = false; saveState(); sendState(); }
void handleSet() {
  int p = g_percent;
  if (server.hasArg("percent")) p = server.arg("percent").toInt();
  applyPercent(p);
  sendState();
}
void handleStatus()  { sendState(); }

void setup() {
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);

  stepper.setMaxSpeed(800);
  stepper.setAcceleration(400);
  stepper.setCurrentPosition(percentToSteps(100)); // старт = раскрыто

  SPIFFS.begin(true);  // включить файловую систему во flash (true = форматировать, если пусто)
  loadState();         // восстановить состояние из файла, если оно сохранялось

  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/grip", handleGrip);
  server.on("/release", handleRelease);
  server.on("/stop", handleStop);
  server.on("/set", handleSet);
  server.on("/status", handleStatus);
  server.begin();
}

void loop() {
  server.handleClient();
  stepper.run();
  // когда двигатель доехал — обновляем состояние
  if (g_moving && stepper.distanceToGo() == 0) {
    g_moving = false;
    digitalWrite(EN_PIN, HIGH); // отключить драйвер (необязательно, экономит ток/нагрев)
  }
}
