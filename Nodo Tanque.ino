#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

/* ============================================================
   CONFIG RENDER
   ============================================================ */
const char* RENDER_URL = "https://appbomba.onrender.com/api/render/update";
const char* AUTH_CODE  = "A9F3K2X7";
unsigned long lastRenderSend = 0;

/* ============================================================
   PINES
   ============================================================ */
#define BTN_MODO    32
#define BTN_BOMBA   33

#define TRIG_TANQUE 26
#define ECHO_TANQUE 27

#define LORA_SS     5
#define LORA_RST    14
#define LORA_DIO0   4

/* ============================================================
   LCD
   ============================================================ */
LiquidCrystal_I2C lcd(0x27, 20, 4);

/* ============================================================
   AP CONFIG
   ============================================================ */
#define AP_SSID "AP-ESP32"
#define AP_PASS "AP14112026"

/* ============================================================
   VARIABLES DEL SISTEMA
   ============================================================ */

// Parámetros configurables
float profPozo  = 2.0;
float altTanque = 1.5;

int minPozo     = 20;
int minTanque   = 70;
int maxTanque   = 98;

// Estados dinámicos
bool modoAuto   = true;
bool bombaON    = false;

bool conexPozo  = false;
int nivelPozo   = 0;
int nivelTanque = 0;

// WiFi guardado
char WIFI_SSID[32] = "";
char WIFI_PASS[64] = "";

// Modos
bool configMode       = false;
unsigned long configStart = 0;

bool viewMode         = false;
unsigned long viewStart = 0;

// LoRa
unsigned long lastLoRa = 0;

// Pantalla sin parpadeos
char line0[21], line1[21], line2[21], line3[21];
char prev0[21], prev1[21], prev2[21], prev3[21];

// Servidor
WebServer server(80);
Preferences prefs;

/* ============================================================
   PROTOTIPOS — NECESARIOS PARA COMPILAR
   ============================================================ */

// Render
void enviarEstadoARender();

// LCD
void actualizarLCDLinea(int row, const char* txt, char* prev);
void actualizarLCD();
void mostrarView();

// Modos
void iniciarConfig();
void iniciarView();

// Sensores
void medirTanque();

// LoRa
void enviarLoRa(String s);
void procesarLoRa(String json);

// Lógica
void logicaBomba();

// Endpoints API
void handleWiFiSet();
void handleGetDatos();
void handleConfig();
void handleModo();
void handleComando();
bool validarToken();
/*************************************************************
 *  NODO MAESTRO TANQUE — PARTE 2
 *  TODAS LAS FUNCIONES
 *************************************************************/
void checkRenderComando() {

    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;

    String url = "https://appbomba.onrender.com/api/render/cmd?auth=A9F3K2X7";
    http.begin(url);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return;
    }

    String payload = http.getString();
    payload.trim();

    // -------------------------------------------------------
    // 1. BUSCAR SI EXISTE ALGÚN "cmd":"XYZ"
    // -------------------------------------------------------
    if (payload.indexOf("\"cmd\":\"ON\"") != -1) {
        enviarLoRa("{\"cmd\":\"ON\"}");
    }

    if (payload.indexOf("\"cmd\":\"OFF\"") != -1) {
        enviarLoRa("{\"cmd\":\"OFF\"}");
    }

    if (payload.indexOf("\"cmd\":\"AUTO\"") != -1) {
        modoAuto = true;
    }

    if (payload.indexOf("\"cmd\":\"MANUAL\"") != -1) {
        modoAuto = false;
    }

    // -------------------------------------------------------
    // 2. PROCESAR SOLO LA ÚLTIMA CONFIG RECIBIDA
    // -------------------------------------------------------
    int lastConfig = payload.lastIndexOf("CONFIG:");
    if (lastConfig != -1) {

        // Extraer solo la parte CONFIG:min_xxx=YYY
        String cfg = payload.substring(lastConfig);
        int end = cfg.indexOf("\"");
        if (end != -1) cfg = cfg.substring(0, end);

        // Ahora cfg = CONFIG:min_xxx=YYY
        cfg.replace("CONFIG:", "");

        int eq = cfg.indexOf('=');
        if (eq != -1) {

            String key = cfg.substring(0, eq);
            String value = cfg.substring(eq + 1);

            key.trim();
            value.trim();

            if (key == "min_pozo") {
                minPozo = value.toInt();
                prefs.putInt("minPozo", minPozo);
            }
            else if (key == "min_tanque") {
                minTanque = value.toInt();
                prefs.putInt("minTanque", minTanque);
            }
            else if (key == "max_tanque") {
                maxTanque = value.toInt();
                prefs.putInt("maxTanque", maxTanque);
            }
            else if (key == "prof_pozo") {
                profPozo = value.toFloat();
                prefs.putFloat("profPozo", profPozo);
            }
            else if (key == "alt_tanque") {
                altTanque = value.toFloat();
                prefs.putFloat("altTanque", altTanque);
            }

            enviarEstadoARender();
        }
    }

    http.end();
}

// Mnejo de cords*/
void sendCORS() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, x-auth-token");
}




/* ============================================================
   LCD — Actualización sin parpadeos
   ============================================================ */
void actualizarLCDLinea(int row, const char* txt, char* prev) {
    if (strcmp(txt, prev) != 0) {
        strcpy(prev, txt);
        lcd.setCursor(0, row);
        lcd.print(txt);
        for (int i = strlen(txt); i < 20; i++) lcd.print(" ");
    }
}

/* ============================================================
   ENVIAR ESTADO A RENDER
   ============================================================ */
void enviarEstadoARender() {

    if (WiFi.status() != WL_CONNECTED) return;

    // cada 8 segundos máximo
    if (millis() - lastRenderSend < 8000) return;
    lastRenderSend = millis();

    HTTPClient http;
    http.begin(RENDER_URL);
    http.addHeader("Content-Type", "application/json");

    String body = "{";
    body += "\"auth\":\""; body += AUTH_CODE; body += "\",";
    body += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    body += "\"nivel_pozo\":" + String(nivelPozo) + ",";
    body += "\"nivel_tanque\":" + String(nivelTanque) + ",";
    body += "\"conexion_pozo\":" + String(conexPozo ? "true" : "false") + ",";
    body += "\"bomba\":" + String(bombaON ? "true":"false") + ",";
    body += "\"modo\":\"" + String(modoAuto ? "AUTO":"MANUAL") + "\",";
    body += "\"min_pozo\":" + String(minPozo) + ",";
    body += "\"min_tanque\":" + String(minTanque) + ",";
    body += "\"max_tanque\":" + String(maxTanque) + ",";
    body += "\"prof_pozo\":" + String(profPozo) + ",";
    body += "\"alt_tanque\":" + String(altTanque);
    body += "}";

    http.POST(body);
    http.end();
}

/* ============================================================
   MEDIR NIVEL TANQUE — ultrasónico
   ============================================================ */
void medirTanque() {

    digitalWrite(TRIG_TANQUE, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_TANQUE, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_TANQUE, LOW);

    long d = pulseIn(ECHO_TANQUE, HIGH, 30000);
    if (d <= 0) return;

    float distancia = (d * 0.034) / 2.0;
    float capacidad = altTanque * 100.0;

    float agua = capacidad - distancia;
    if (agua < 0) agua = 0;
    if (agua > capacidad) agua = capacidad;

    nivelTanque = (agua / capacidad) * 100.0;
    nivelTanque = constrain(nivelTanque, 0, 100);
}

/* ============================================================
   LORA — envío simple
   ============================================================ */
void enviarLoRa(String s) {
    LoRa.beginPacket();
    LoRa.print(s);
    LoRa.endPacket();
}

/* ============================================================
   LORA — procesamiento del paquete recibido
   ============================================================ */
void procesarLoRa(String json) {

    int p = json.indexOf("\"p\":");
    if (p != -1) {
        nivelPozo = json.substring(p + 4).toInt();
        nivelPozo = constrain(nivelPozo, 0, 100);
    }

    int b = json.indexOf("\"b\":");
    if (b != -1) {
        int v = json.substring(b + 4).toInt();
        bombaON = (v == 1);
    }

    conexPozo = true;
    lastLoRa = millis();
}

/* ============================================================
   LÓGICA DE BOMBA — AUTO Y MANUAL
   ============================================================ */
void logicaBomba() {

    if (modoAuto) {

        if (conexPozo && nivelPozo > minPozo) {

            if (!bombaON && nivelTanque < minTanque) {
                enviarLoRa("{\"cmd\":\"ON\"}");
            }

            if (bombaON && (nivelPozo <= minPozo || nivelTanque >= maxTanque)) {
                enviarLoRa("{\"cmd\":\"OFF\"}");
            }

        } else {
            if (bombaON)
                enviarLoRa("{\"cmd\":\"OFF\"}");
        }
    }
    else {
        static bool last = HIGH;
        bool btn = digitalRead(BTN_BOMBA);

        if (last == HIGH && btn == LOW) {
            delay(40);
            if (digitalRead(BTN_BOMBA) == LOW) {
                if (!bombaON) {
                    if (conexPozo && nivelPozo > minPozo)
                        enviarLoRa("{\"cmd\":\"ON\"}");
                } else {
                    enviarLoRa("{\"cmd\":\"OFF\"}");
                }
            }
        }
        last = btn;

        // seguridad
        if (bombaON && nivelPozo <= minPozo)
            enviarLoRa("{\"cmd\":\"OFF\"}");
    }
}

/* ============================================================
   ACTUALIZAR LCD
   ============================================================ */
void actualizarLCD() {

    sprintf(line0, "Red:%s Pozo:%s",
            WiFi.status()==WL_CONNECTED ? "WIFI" : "AP",
            conexPozo ? "OK" : "XX");

    sprintf(line1, "Tanq:%3d%%  Pozo:%3d%%", nivelTanque, nivelPozo);

    sprintf(line2, "Modo:%s", modoAuto ? "  Automatico" : "  Manual");

    sprintf(line3, "Bomba:%s", bombaON ? "  Encendida " : "  Apagada");

    actualizarLCDLinea(0, line0, prev0);
    actualizarLCDLinea(1, line1, prev1);
    actualizarLCDLinea(2, line2, prev2);
    actualizarLCDLinea(3, line3, prev3);
}

/* ============================================================
   MODO CONFIG
   ============================================================ */
void iniciarConfig() {
    configMode = true;
    configStart = millis();

    lcd.clear();
    lcd.setCursor(0,0); lcd.print("ACCES POIN");
    lcd.setCursor(0,1); lcd.print("SSID: "); lcd.print(AP_SSID);
    lcd.setCursor(0,2); lcd.print("PASS: "); lcd.print(AP_PASS);
    lcd.setCursor(0,3); lcd.print("TOKEN: "); lcd.print(AUTH_CODE);
}

/* ============================================================
   MODO VISUALIZACION
   ============================================================ */
void mostrarView() {
    
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("IP: "); lcd.print(WiFi.localIP());
    lcd.setCursor(0,1); lcd.print("TOKEN: "); lcd.print(AUTH_CODE);
    lcd.setCursor(0,2);
    lcd.print("Pozo:"); lcd.print(minPozo);
    lcd.print(" Tq:"); lcd.print(minTanque);

    lcd.setCursor(0,3);
    lcd.print("MaxT:"); lcd.print(maxTanque);
}

void iniciarView() {
    viewMode = true;
    viewStart = millis();
    mostrarView();
}

/* ============================================================
   VALIDAR TOKEN
   ============================================================ */
bool validarToken() {

    if (server.hasHeader("x-auth-token"))
        if (server.header("x-auth-token") == AUTH_CODE)
            return true;

    if (server.hasArg("auth"))
        if (server.arg("auth") == AUTH_CODE)
            return true;

    return false;
}

/* ============================================================
   ENDPOINT — GUARDAR WIFI
   ============================================================ */
void handleWiFiSet() {

    sendCORS();   // <-- CORS

    if (!validarToken()) {
        server.send(401, "application/json", "{\"error\":\"token\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, server.arg("plain"));

    const char* s = doc["ssid"] | "";
    const char* p = doc["pass"] | "";

    prefs.putString("wifi_ssid", s);
    prefs.putString("wifi_pass", p);

    server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}


/* ============================================================
   ENDPOINT — GET DATOS
   ============================================================ */
void handleGetDatos() {

    sendCORS();   // <-- CORS

    if (!validarToken()) {
        server.send(401, "application/json", "{\"error\":\"token\"}");
        return;
    }

    StaticJsonDocument<256> doc;

    doc["nivel_pozo"]   = nivelPozo;
    doc["nivel_tanque"] = nivelTanque;
    doc["conexion_pozo"]= conexPozo;
    doc["bomba"]        = bombaON;
    doc["modo"]         = modoAuto ? "AUTO" : "MANUAL";
    doc["min_pozo"]     = minPozo;
    doc["min_tanque"]   = minTanque;
    doc["max_tanque"]   = maxTanque;
    doc["prof_pozo"]    = profPozo;
    doc["alt_tanque"]   = altTanque;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);

    enviarEstadoARender();
}


/* ============================================================
   ENDPOINT — CONFIG
   ============================================================ */
void handleConfig() {

    sendCORS();   // <-- CORS

    if (!validarToken()) {
        server.send(401, "application/json", "{\"error\":\"token\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, server.arg("plain"));

    bool enviarPozo = false;

    if (doc.containsKey("min_pozo")) {
        minPozo = doc["min_pozo"];
        prefs.putInt("minPozo", minPozo);
        enviarPozo = true;
    }

    if (doc.containsKey("min_tanque")) {
        minTanque = doc["min_tanque"];
        prefs.putInt("minTanque", minTanque);
    }

    if (doc.containsKey("max_tanque")) {
        maxTanque = doc["max_tanque"];
        prefs.putInt("maxTanque", maxTanque);
    }

    if (doc.containsKey("prof_pozo")) {
        profPozo = doc["prof_pozo"];
        prefs.putFloat("profPozo", profPozo);
        enviarPozo = true;
    }

    if (doc.containsKey("alt_tanque")) {
        altTanque = doc["alt_tanque"];
        prefs.putFloat("altTanque", altTanque);
    }

    if (enviarPozo) {
        String j = "{\"min\":";
        j += minPozo;
        j += ",\"prof\":";
        j += profPozo;
        j += "}";
        enviarLoRa(j);
    }

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender();
}


/* ============================================================
   ENDPOINT — CAMBIO DE MODO
   ============================================================ */
void handleModo() {

    sendCORS();   // <-- CORS

    if (!validarToken()) {
        server.send(401, "application/json", "{\"error\":\"token\"}");
        return;
    }

    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));

    String m = doc["modo"];

    if (m == "AUTO") modoAuto = true;
    else if (m == "MANUAL") modoAuto = false;

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender();
}

/* ============================================================
   ENDPOINT — COMANDO ON/OFF
   ============================================================ */
void handleComando() {

    sendCORS();   // <-- CORS

    if (!validarToken()) {
        server.send(401, "application/json", "{\"error\":\"token\"}");
        return;
    }

    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));

    String c = doc["cmd"];

    if (c == "ON") {

        if (modoAuto) {
            server.send(403, "application/json", "{\"error\":\"auto\"}");
            return;
        }

        if (conexPozo && nivelPozo > minPozo)
            enviarLoRa("{\"cmd\":\"ON\"}");
    }
    else if (c == "OFF") {
        enviarLoRa("{\"cmd\":\"OFF\"}");
    }

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender();
}

/*************************************************************
 *  NODO MAESTRO TANQUE — PARTE 3
 *  SETUP() + LOOP()
 *************************************************************/

void setup() {

    Serial.begin(115200);

    /* ================= LCD ================= */
    lcd.init();
    lcd.backlight();

    /* ================= PINES ================= */
    pinMode(BTN_MODO,  INPUT_PULLUP);
    pinMode(BTN_BOMBA, INPUT_PULLUP);
    pinMode(TRIG_TANQUE, OUTPUT);
    pinMode(ECHO_TANQUE, INPUT);

    /* ================= PREFERENCIAS ================= */
    prefs.begin("sistema", false);

    String ss = prefs.getString("wifi_ssid", "");
    String ps = prefs.getString("wifi_pass", "");

    if (ss.length() > 0) strcpy(WIFI_SSID, ss.c_str());
    if (ps.length() > 0) strcpy(WIFI_PASS, ps.c_str());

    minPozo   = prefs.getInt("minPozo", minPozo);
    minTanque = prefs.getInt("minTanque", minTanque);
    maxTanque = prefs.getInt("maxTanque", maxTanque);
    profPozo  = prefs.getFloat("profPozo", profPozo);
    altTanque = prefs.getFloat("altTanque", altTanque);

    /* ================= LORA ================= */
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    LoRa.begin(433E6);
    LoRa.setSyncWord(0xF3);

    /* ================= WIFI ================= */
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 5000) {
        delay(100);
    }

    // Si no conecta → activar AP
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.softAP(AP_SSID, AP_PASS);
    }

    /* ================= API LOCAL ================= */
    server.on("/api/wifi",    HTTP_POST, handleWiFiSet);
    server.on("/api/datos",   HTTP_GET,  handleGetDatos);
    server.on("/api/config",  HTTP_POST, handleConfig);
    server.on("/api/comando", HTTP_POST, handleComando);
    server.on("/api/modo",    HTTP_POST, handleModo);
    server.begin();
    server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
        sendCORS();
        server.send(204);
        return;
    }
    server.send(404, "text/plain", "Not found");
});


    lcd.clear();
}

/*************************************************************
 *  LOOP PRINCIPAL
 *************************************************************/
void loop() {

static unsigned long lastCmdCheck = 0;
if (millis() - lastCmdCheck > 5000) {
    checkRenderComando();
    lastCmdCheck = millis();
}

    /* ========================================================
       BOTÓN BOMBA — LARGO PARA CONFIG MODE
       ======================================================== */
    static bool bHold = false;
    static unsigned long bTime = 0;

    bool b = digitalRead(BTN_BOMBA);

    if (!configMode && b == LOW && !bHold) {
        bHold = true;
        bTime = millis();
    }

    if (b == HIGH && bHold) {
        // pulsación corta: control manual (se maneja dentro de lógica)
        bHold = false;
    }

    if (bHold && millis() - bTime >= 3000 && !configMode) {
        iniciarConfig();
        bHold = false;
    }

    /* ========================================================
       BOTÓN MODO — LARGO PARA VIEW MODE
       ======================================================== */
    static bool mHold = false;
    static unsigned long mTime = 0;

    bool m = digitalRead(BTN_MODO);

    if (m == LOW && !mHold) {
        mHold = true;
        mTime = millis();
    }

    if (m == HIGH && mHold) {
        // pulsación corta → cambia modo
        if (millis() - mTime < 5000 && !viewMode) {
            modoAuto = !modoAuto;
            enviarEstadoARender();
        }
        mHold = false;
    }

    if (mHold && millis() - mTime >= 5000 && !viewMode) {
        iniciarView();
        mHold = false;
    }

    /* ========================================================
       MODO CONFIG
       ======================================================== */
    if (configMode) {

        // dura 3 minutos
        if (millis() - configStart > 180000) {
            configMode = false;
            lcd.clear();
        }

        // permitir API durante config
        server.handleClient();
        return;
    }

    /* ========================================================
       MODO VISUALIZACIÓN (30s)
       ======================================================== */
    if (viewMode) {

        if (millis() - viewStart > 30000) {
            viewMode = false;
            lcd.clear();
        }
        else {
            mostrarView();
        }
    }

    /* ========================================================
       RECEPCIÓN LORA
       ======================================================== */
    int p = LoRa.parsePacket();
    if (p) {
        String j = "";
        while (LoRa.available()) {
            char c = LoRa.read();
            if (c >= 32 && c <= 126) j += c;
        }

        if (j.length()) {
            procesarLoRa(j);
            enviarEstadoARender();  
        }
    }

    // marcar desconectado
    if (millis() - lastLoRa > 30000) {
        conexPozo = false;
    }

    /* ========================================================
       MEDIR TANQUE
       ======================================================== */
    static unsigned long lastTank = 0;

    if (millis() - lastTank > 2000) {
        medirTanque();
        lastTank = millis();
    }

    /* ========================================================
       CONTROL AUTOMÁTICO / MANUAL
       ======================================================== */
    logicaBomba();

    /* ========================================================
       WIFI y AP
       ======================================================== */
    bool wifiOK = WiFi.status() == WL_CONNECTED;

    if (!wifiOK && WiFi.getMode() != WIFI_AP_STA) {
        WiFi.softAP(AP_SSID, AP_PASS);
    }
    else if (wifiOK) {
        WiFi.softAPdisconnect(true);
    }

    /* ========================================================
       ACTUALIZAR LCD
       ======================================================== */
    static unsigned long lastLCD = 0;

    if (!viewMode && millis() - lastLCD > 300) {
        actualizarLCD();
        lastLCD = millis();
    }

    /* ========================================================
       SERVIDOR API
       ======================================================== */
    server.handleClient();

    delay(10);
}