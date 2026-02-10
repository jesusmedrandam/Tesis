#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>

// ================= CONFIGURACIÓN =================

// Pines ultrasónico pozo
#define TRIG_POZO 26
#define ECHO_POZO 27

// Relevador bomba (LOW = ON, HIGH = OFF)
#define RELAY_PIN 25

// Pines LoRa
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  4 

// ================= VARIABLES =================

Preferences prefs;

int nivelPozo = 0;
bool bombaEstado = false;

// Configuración recibida del maestro
float profPozoConfig = 2.0;  // metros
int minPozoConfig = 20;      // porcentaje

// Temporizadores
unsigned long ultimaComunicacion = 0;
const unsigned long TIMEOUT_LORA = 30000;       // 30s sin maestro → bomba OFF
const unsigned long HEARTBEAT_INTERVAL = 8000;  // envío periódico al maestro
const unsigned long MEDICION_INTERVAL = 3000;   // cada 3s mide el pozo

// ================= FUNCIONES =================

// Guardar configuración en memoria
void guardarConfig() {
    prefs.putFloat("profPozo", profPozoConfig);
    prefs.putInt("minPozo", minPozoConfig);
}

// Medir nivel del pozo
void medirNivel() {

    digitalWrite(TRIG_POZO, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_POZO, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_POZO, LOW);

    long duracion = pulseIn(ECHO_POZO, HIGH, 30000);

    if (duracion <= 0) return;

    float distancia = (duracion * 0.034) / 2.0;       // cm
    float alturaMax = profPozoConfig * 100.0;         // cm
    float alturaAgua = alturaMax - distancia;

    if (alturaAgua < 0) alturaAgua = 0;
    if (alturaAgua > alturaMax) alturaAgua = alturaMax;

    nivelPozo = (alturaAgua / alturaMax) * 100.0;
    nivelPozo = constrain(nivelPozo, 0, 100);
}

// Enviar estado al maestro en formato correcto
void enviarEstado() {

    LoRa.beginPacket();
    LoRa.print("{\"p\":");
    LoRa.print(nivelPozo);
    LoRa.print(",\"b\":");
    LoRa.print(bombaEstado ? 1 : 0);
    LoRa.print("}");
    LoRa.endPacket();

    Serial.printf("→ Estado enviado: Nivel=%d%% | Bomba=%s\n",
                    nivelPozo, bombaEstado ? "ON" : "OFF");
}

// Procesar configuración JSON desde el maestro
void procesarConfiguracion(String json) {

    int posMin = json.indexOf("\"min\":");
    int posProf = json.indexOf("\"prof\":");

    if (posMin != -1) {
        minPozoConfig = json.substring(posMin + 6).toInt();
    }

    if (posProf != -1) {
        profPozoConfig = json.substring(posProf + 7).toFloat();
    }

    guardarConfig();
    enviarEstado();

    Serial.println("✔ Configuración actualizada desde maestro");
}

// ================= SETUP =================
void setup() {

    Serial.begin(115200);
    Serial.println("\n=== NODO POZO INICIANDO ===");

    // Cargar configuración guardada
    prefs.begin("pozo", false);
    profPozoConfig = prefs.getFloat("profPozo", 2.0);
    minPozoConfig = prefs.getInt("minPozo", 20);

    // Configurar pines
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);   // bomba OFF

    pinMode(TRIG_POZO, OUTPUT);
    pinMode(ECHO_POZO, INPUT);

    // Inicializar LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(433E6)) {
        Serial.println("ERROR LoRa");
        while (1);
    }

    LoRa.setSyncWord(0xF3);
    LoRa.enableCrc();

    ultimaComunicacion = millis();

    medirNivel();
    enviarEstado();
}

// ================= LOOP =================
void loop() {

    static unsigned long lastMed = 0;
    static unsigned long lastHB = 0;

    // =========================================================
    // 1. Escuchar mensajes del maestro
    // =========================================================
    int packetSize = LoRa.parsePacket();
    if (packetSize) {

        String msg = "";
        while (LoRa.available()) {
            char c = LoRa.read();
            if (c >= 32 && c <= 126) msg += c;
        }

        ultimaComunicacion = millis();
        Serial.println("Mensaje recibido: " + msg);

        // ------- COMANDOS DEL MAESTRO -------
        if (msg.indexOf("\"cmd\"") != -1) {

            // Encender bomba
            if (msg.indexOf("ON") != -1) {

                if (nivelPozo >= minPozoConfig) {
                    bombaEstado = true;
                    digitalWrite(RELAY_PIN, LOW);
                }

                enviarEstado();
            }

            // Apagar bomba
            else if (msg.indexOf("OFF") != -1) {
                bombaEstado = false;
                digitalWrite(RELAY_PIN, HIGH);
                enviarEstado();
            }
        }

        // ------- CONFIGURACIÓN JSON -------
        else if (msg.indexOf("\"min\"") != -1 || msg.indexOf("\"prof\"") != -1) {
            procesarConfiguracion(msg);
        }

        // ------- PING / STATUS -------
        else if (msg == "PING" || msg == "STATUS") {
            enviarEstado();
        }
    }

    // =========================================================
    // 2. Seguridad: pérdida de comunicación → bomba OFF
    // =========================================================
    if (bombaEstado && millis() - ultimaComunicacion > TIMEOUT_LORA) {
        bombaEstado = false;
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("⚠ Timeout maestro → Bomba OFF");
        enviarEstado();
    }

    // =========================================================
    // 3. Heartbeat periódico hacia el maestro
    // =========================================================
    if (millis() - lastHB > HEARTBEAT_INTERVAL) {
        enviarEstado();
        lastHB = millis();
    }

    // =========================================================
    // 4. Medición del nivel del pozo
    // =========================================================
    if (millis() - lastMed > MEDICION_INTERVAL) {

        medirNivel();
        lastMed = millis();

        // Seguridad: nivel muy bajo
        if (bombaEstado && nivelPozo < minPozoConfig) {
            bombaEstado = false;
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println(" Nivel bajo → Bomba OFF");
            enviarEstado();
        }
    }

    delay(10);
}