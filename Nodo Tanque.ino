#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>


const char* RENDER_URL = "https://appbomba.onrender.com/api/render/update";
const char* AUTH_CODE  = "A9F3K2X7";
unsigned long lastRenderSend = 0;


#define BTN_MODO    32
#define BTN_BOMBA   33
#define TRIG_TANQUE 26
#define ECHO_TANQUE 27
#define LORA_SS     5
#define LORA_RST    14
#define LORA_DIO0   4

LiquidCrystal_I2C lcd(0x27, 20, 4);

#define AP_SSID "AP-ESP32"
#define AP_PASS "AP14112026"

float profPozo  = 2.0;
float altTanque = 1.5;

int minPozo     = 20;
int minTanque   = 70;
int maxTanque   = 98;

bool modoAuto   = true;
bool bombaON    = false;
bool conexPozo  = false;

int nivelPozo   = 0;
int nivelTanque = 0;

char WIFI_SSID[32] = "";
char WIFI_PASS[64] = "";

bool configMode = false;
unsigned long configStart = 0;

bool viewMode   = false;
unsigned long viewStart = 0;

unsigned long lastLoRa = 0;


char line0[21], line1[21], line2[21], line3[21];
char prev0[21], prev1[21], prev2[21], prev3[21];


bool lcdTransicion = false;
unsigned long lcdTransicionTime = 0;


Preferences prefs;
WebServer server(80);


void resetLCDCache() {
    memset(prev0, 0, 21);
    memset(prev1, 0, 21);
    memset(prev2, 0, 21);
    memset(prev3, 0, 21);
}

void actualizarLCDLinea(int row, const char* txt, char* prev) {
    if (strcmp(txt, prev) != 0) {
        strcpy(prev, txt);
        lcd.setCursor(0, row);
        lcd.print(txt);
        for (int i = strlen(txt); i < 20; i++) lcd.print(" ");
    }
}

void actualizarLCD() {

    if (lcdTransicion) {
    if (millis() - lcdTransicionTime < 1500) {
        return;
    } else {
        lcdTransicion = false;
        resetLCDCache();
    }
}

    sprintf(line0, "Red:%s Pozo:%s",
            WiFi.status()==WL_CONNECTED ? "WIFI" : "AP",
            conexPozo ? "OK" : "XX");

    sprintf(line1, "Tanq:%3d%%  Pozo:%3d%%", nivelTanque, nivelPozo);

    sprintf(line2, "Modo:%s", modoAuto ? "  Automatico" : "  Manual");

    sprintf(line3, "Bomba:%s", bombaON ? "  Encendida " : "  Apagada  ");

    actualizarLCDLinea(0, line0, prev0);
    actualizarLCDLinea(1, line1, prev1);
    actualizarLCDLinea(2, line2, prev2);
    actualizarLCDLinea(3, line3, prev3);
}


void enviarEstadoARender(bool forzar = false) {

    if (WiFi.status() != WL_CONNECTED) return;

    if (!forzar && millis() - lastRenderSend < 8000) return;

    lastRenderSend = millis();

    HTTPClient http;
    http.begin(RENDER_URL);
    http.addHeader("Content-Type", "application/json");

    String body = "{";
    body += "\"auth\":\"" + String(AUTH_CODE) + "\",";
    body += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    body += "\"nivel_pozo\":" + String(nivelPozo) + ",";
    body += "\"nivel_tanque\":" + String(nivelTanque) + ",";
    body += "\"conexion_pozo\":" + String(conexPozo?"true":"false") + ",";
    body += "\"bomba\":" + String(bombaON?"true":"false") + ",";
    body += "\"modo\":\"" + String(modoAuto?"AUTO":"MANUAL") + "\",";
    body += "\"min_pozo\":" + String(minPozo) + ",";
    body += "\"min_tanque\":" + String(minTanque) + ",";
    body += "\"max_tanque\":" + String(maxTanque) + ",";
    body += "\"prof_pozo\":" + String(profPozo) + ",";
    body += "\"alt_tanque\":" + String(altTanque);
    body += "}";

    http.POST(body);
    http.end();
}



void medirTanque() {

    const float offsetSensor = 20.0; // cm (sensor 20 cm de punto ciego)

    digitalWrite(TRIG_TANQUE, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG_TANQUE, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG_TANQUE, LOW);

    long d = pulseIn(ECHO_TANQUE, HIGH, 30000);
    if (d <= 0) return;

    float dist = (d * 0.034) / 2.0;  

    float altura = altTanque * 100.0; 

    float distVacio = altura + offsetSensor;
    float distLleno = offsetSensor;

    float rango = distVacio - distLleno;

    float nivel = (distVacio - dist) / rango;

    nivelTanque = nivel * 100.0;

    nivelTanque = constrain(nivelTanque, 0, 100);
}


void enviarLoRa(String s) {
    LoRa.beginPacket();
    LoRa.print(s);
    LoRa.endPacket();
}

void procesarLoRa(String json) {

    bool prev = bombaON;

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

    if (bombaON != prev) {
        lcdTransicion = false;  
        resetLCDCache();
        actualizarLCD();
    }
}


void mostrarView() {

    lcd.setCursor(0,0);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP());

    lcd.setCursor (0,1);
    lcd.print("Codigo: ");
    lcd.print(AUTH_CODE);
    
    lcd.setCursor(0,2);
    lcd.print("Pozo: Min");
    lcd.print(minPozo);
    lcd.print(" Prf:");
    lcd.print(profPozo,1);

    lcd.setCursor(0,3);
    lcd.print("Tq: Mi");
    lcd.print(minTanque);
    lcd.print(" Mx");
    lcd.print(maxTanque);
    lcd.print(" Al");
    lcd.print(altTanque,1);

    
}

void iniciarConfig() {
    configMode = true;
    configStart = millis();
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(" MODO CONFIGURACION ");
    lcd.setCursor(0,1); lcd.print("SSID: "); lcd.print(AP_SSID);
    lcd.setCursor(0,2); lcd.print("PASS: "); lcd.print(AP_PASS);
    lcd.setCursor(0,3); lcd.print("CODIGO: "); lcd.print(AUTH_CODE);
}

void iniciarView() {
    viewMode = true;
    viewStart = millis();
    lcd.clear();
}


bool validarToken() {
    if (server.hasHeader("x-auth-token"))
        if (server.header("x-auth-token") == AUTH_CODE)
            return true;

    if (server.hasArg("auth"))
        if (server.arg("auth") == AUTH_CODE)
            return true;

    return false;
}

void sendCORS() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, x-auth-token");
}


void checkRenderComando() {

    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = "https://appbomba.onrender.com/api/render/cmd?auth=A9F3K2X7";

    http.begin(url);
    http.setTimeout(500); 

    int code = http.GET();
    if (code != 200) { 
        http.end(); 
        return; 
    }

    String payload = http.getString();
    http.end();   // 👈 MOVER AQUÍ (IMPORTANTE)
    payload.trim();

    if (payload.indexOf("\"cmd\":\"ON\"") != -1) {
        lcd.setCursor(0,3); lcd.print("Bomba: Encendiendo ");
        lcdTransicion = true;
        lcdTransicionTime = millis();
        enviarLoRa("{\"cmd\":\"ON\"}");
        enviarEstadoARender(true);
    }

    if (payload.indexOf("\"cmd\":\"OFF\"") != -1) {
        lcd.setCursor(0,3); lcd.print("Bomba: Apagando    ");
        lcdTransicion = true;
        lcdTransicionTime = millis();
        enviarLoRa("{\"cmd\":\"OFF\"}");
        enviarEstadoARender(true);
    }

    if (payload.indexOf("\"cmd\":\"AUTO\"") != -1) {
        modoAuto = true;
        enviarEstadoARender(true);
    }

    if (payload.indexOf("\"cmd\":\"MANUAL\"") != -1) {
        modoAuto = false;
        enviarEstadoARender(true);
    }

    // CONFIG
    int lastConfig = payload.lastIndexOf("CONFIG:");
    if (lastConfig != -1) {

        String cfg = payload.substring(lastConfig);
        int end = cfg.indexOf("\"");
        if (end != -1) cfg = cfg.substring(0, end);

        cfg.replace("CONFIG:", "");
        int eq = cfg.indexOf('=');
        if (eq != -1) {

            String key = cfg.substring(0, eq);
            String value = cfg.substring(eq + 1);
            key.trim(); value.trim();

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

            enviarEstadoARender(true);
        }
    }
}

void handleWiFiSet() {
    sendCORS();
    if (!validarToken()) { server.send(401,"application/json","{\"error\":\"token\"}"); return; }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, server.arg("plain"));

    prefs.putString("wifi_ssid", doc["ssid"] | "");
    prefs.putString("wifi_pass", doc["pass"] | "");

    server.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

void handleGetDatos() {
    sendCORS();
    if (!validarToken()) { server.send(401,"application/json","{\"error\":\"token\"}"); return; }

    StaticJsonDocument<256> doc;

    doc["nivel_pozo"] = nivelPozo;
    doc["nivel_tanque"] = nivelTanque;
    doc["conexion_pozo"] = conexPozo;
    doc["bomba"] = bombaON;
    doc["modo"] = modoAuto?"AUTO":"MANUAL";
    doc["min_pozo"] = minPozo;
    doc["min_tanque"] = minTanque;
    doc["max_tanque"] = maxTanque;
    doc["prof_pozo"] = profPozo;
    doc["alt_tanque"] = altTanque;

    String out;
    serializeJson(doc, out);

    server.send(200, "application/json", out);
    enviarEstadoARender(true);
}

void handleConfig() {
    sendCORS();
    if (!validarToken()) { server.send(401,"application/json","{\"error\":\"token\"}"); return; }

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
        String j = "{\"min\":" + String(minPozo) + ",\"prof\":" + String(profPozo) + "}";
        enviarLoRa(j);
    }

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender(true);
}

void handleModo() {
    sendCORS();
    if (!validarToken()) { server.send(401,"application/json","{\"error\":\"token\"}"); return; }

    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));

    String m = doc["modo"];

    if (m == "AUTO") modoAuto = true;
    else if (m == "MANUAL") modoAuto = false;

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender(true);
}

void handleComando() {
    sendCORS();
    if (!validarToken()) { server.send(401,"application/json","{\"error\":\"token\"}"); return; }

    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));

    String c = doc["cmd"];

    if (c == "ON") {
        if (modoAuto) { server.send(403,"application/json","{\"error\":\"auto\"}"); return; }

        if (conexPozo && nivelPozo > minPozo) {
            lcd.setCursor(0,3); lcd.print("Bomba: Encendiendo ");
            lcdTransicion = true;
            lcdTransicionTime = millis();
            enviarLoRa("{\"cmd\":\"ON\"}");
        }
    }
    else if (c == "OFF") {
        lcd.setCursor(0,3); lcd.print("Bomba: Apagando    ");
        lcdTransicion = true;
        lcdTransicionTime = millis();
        enviarLoRa("{\"cmd\":\"OFF\"}");
    }

    server.send(200, "application/json", "{\"ok\":true}");
    enviarEstadoARender(true);
}


void logicaBomba() {
    //  MODO MANUAL
    if (!modoAuto) {

        static bool last = HIGH;
        bool btn = digitalRead(BTN_BOMBA);

        // Pulsación corta = encender/apagar
        if (last == HIGH && btn == LOW) {
            delay(40);
            if (digitalRead(BTN_BOMBA) == LOW) {

                if (!bombaON) {
                    // Mostrar mensaje de transición
                    lcd.setCursor(0,3); lcd.print("Bomba: Encendiendo ");
                    lcdTransicion = true;
                    lcdTransicionTime = millis();
                    enviarLoRa("{\"cmd\":\"ON\"}");
                } else {
                    lcd.setCursor(0,3); lcd.print("Bomba: Apagando    ");
                    lcdTransicion = true;
                    lcdTransicionTime = millis();
                    enviarLoRa("{\"cmd\":\"OFF\"}");
                }
            }
        }
        last = btn;

        if (bombaON && nivelPozo <= minPozo) {
            enviarLoRa("{\"cmd\":\"OFF\"}");
        }

        return; 
    }

    //  MODO AUTOMÁTICO
    if (conexPozo && nivelPozo > minPozo) {

        static bool ordenEncendido = false;

if (!bombaON && nivelTanque < minTanque) {
    if (!ordenEncendido) {
        lcd.setCursor(0,3); lcd.print("Bomba: Encendiendo ");
        lcdTransicion = true;
        lcdTransicionTime = millis();
        enviarLoRa("{\"cmd\":\"ON\"}");
        ordenEncendido = true;
    }
} else {
    ordenEncendido = false;
}

        static bool ordenApagado = false;

if (bombaON && (nivelPozo <= minPozo || nivelTanque >= maxTanque)) {
    if (!ordenApagado) {
        lcd.setCursor(0,3); lcd.print("Bomba: Apagando    ");
        lcdTransicion = true;
        lcdTransicionTime = millis();
        enviarLoRa("{\"cmd\":\"OFF\"}");
        ordenApagado = true;
    }
} else {
    ordenApagado = false;
}

    } else {
        if (bombaON) {
            lcd.setCursor(0,3); lcd.print("Bomba: Apagando    ");
            lcdTransicion = true;
            lcdTransicionTime = millis();
            enviarLoRa("{\"cmd\":\"OFF\"}");
        }
    }
}

void setup() {

    Serial.begin(115200);
    lcd.init();
    lcd.backlight();

    pinMode(BTN_MODO, INPUT_PULLUP);
    pinMode(BTN_BOMBA, INPUT_PULLUP);
    pinMode(TRIG_TANQUE, OUTPUT);
    pinMode(ECHO_TANQUE, INPUT);

    prefs.begin("sistema", false);

    String ss = prefs.getString("wifi_ssid", "");
    String ps = prefs.getString("wifi_pass", "");
    if (ss.length()) strcpy(WIFI_SSID, ss.c_str());
    if (ps.length()) strcpy(WIFI_PASS, ps.c_str());

    minPozo   = prefs.getInt("minPozo", minPozo);
    minTanque = prefs.getInt("minTanque", minTanque);
    maxTanque = prefs.getInt("maxTanque", maxTanque);
    profPozo  = prefs.getFloat("profPozo", profPozo);
    altTanque = prefs.getFloat("altTanque", altTanque);

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    LoRa.begin(433E6);
    LoRa.setSyncWord(0xF3);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 5000) delay(100);

    if (WiFi.status() != WL_CONNECTED) WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/api/wifi",    HTTP_POST, handleWiFiSet);
    server.on("/api/datos",   HTTP_GET,  handleGetDatos);
    server.on("/api/config",  HTTP_POST, handleConfig);
    server.on("/api/comando", HTTP_POST, handleComando);
    server.on("/api/modo",    HTTP_POST, handleModo);

    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) { sendCORS(); server.send(204); return; }
        server.send(404, "text/plain", "Not found");
    });

    server.begin();
    lcd.clear();
    resetLCDCache();
}

void loop() {
   /* RENDER */
static unsigned long lastCmdCheck = 0;

if (millis() - lastCmdCheck > 8000 && !lcdTransicion) {
    checkRenderComando();
    lastCmdCheck = millis();
}

    /* BOTÓN BOMBA */
    static bool bHold = false;
    static unsigned long bTime = 0;
    bool b = digitalRead(BTN_BOMBA);

    if (!configMode && b == LOW && !bHold) { bHold = true; bTime = millis(); }
    if (b == HIGH && bHold) bHold = false;

    if (bHold && millis() - bTime >= 3000 && !configMode) {
        iniciarConfig();
        bHold = false;
    }

    /* BOTÓN MODO */
    static bool mHold = false;
    static unsigned long mTime = 0;
    bool m = digitalRead(BTN_MODO);

    if (m == LOW && !mHold) { mHold = true; mTime = millis(); }

    if (m == HIGH && mHold) {
        if (millis() - mTime < 5000 && !viewMode) {
            modoAuto = !modoAuto;
            enviarEstadoARender(true);
            resetLCDCache();
        }
        mHold = false;
    }

    if (mHold && millis() - mTime >= 5000 && !viewMode) {
        iniciarView();
        mHold = false;
    }

    /* CONFIG MODE */
    if (configMode) {

        if (millis() - configStart > 180000) {
            configMode = false;
            lcd.clear();
            resetLCDCache();
            actualizarLCD(); 
        }

        if (digitalRead(BTN_MODO) == LOW) {
            configMode = false;
            lcd.clear();
            resetLCDCache();
            actualizarLCD(); 
        }

        server.handleClient();
        return;
    }

    /* VIEW MODE */
    static bool btnReleased = false;

if (viewMode) {

    // esperar que el botón se suelte primero
    if (!btnReleased && digitalRead(BTN_MODO) == HIGH) {
        btnReleased = true;
    }

    // salir solo si vuelve a presionar
    if (btnReleased && digitalRead(BTN_MODO) == LOW) {
        viewMode = false;
        btnReleased = false;

        lcd.clear();
        resetLCDCache();
        actualizarLCD();
        delay(200);
    }

    // timeout
    if (millis() - viewStart > 30000) {
        viewMode = false;
        btnReleased = false;

        lcd.clear();
        resetLCDCache();
        actualizarLCD();
    }
    else {
        mostrarView();
    }

    server.handleClient();
    return;
}




    /* LoRa PING */
    static unsigned long lastPing = 0;
    if (millis() - lastPing > 5000) {
        enviarLoRa("PING");
        lastPing = millis();
    }

    /* RECEPCIÓN LoRa */
    int p = LoRa.parsePacket();
    if (p) {
        String j = "";
        while (LoRa.available()) {
            char c = LoRa.read();
            if (c >= 32 && c <= 126) j += c;
        }

        if (j.length()) {
            procesarLoRa(j);
            enviarEstadoARender(true);
        }
    }

    if (millis() - lastLoRa > 30000) {
    conexPozo = false;
    bombaON = false;
}

    /* MEDIR TANQUE */
    static unsigned long lastTank = 0;
    if (millis() - lastTank > 2000) {
        medirTanque();
        lastTank = millis();
    }

    /* LÓGICA BOMBA */
    logicaBomba();

    /* WIFI */
    static bool apActivo = false;

if (WiFi.status() != WL_CONNECTED && !apActivo) {
    WiFi.softAP(AP_SSID, AP_PASS);
    apActivo = true;
}

if (WiFi.status() == WL_CONNECTED && apActivo) {
    WiFi.softAPdisconnect(true);
    apActivo = false;
}
    /* LCD */
    static unsigned long lastLCD = 0;
    if (!viewMode && millis() - lastLCD > 300) {
        actualizarLCD();
        lastLCD = millis();
    }

    server.handleClient();
    delay(10);
}
