#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>

#define TRIG_POZO 26
#define ECHO_POZO 27
#define RELAY_PIN 25
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  4 

Preferences prefs;
int nivelPozo = 0;
bool bombaEstado = false;

float profPozoConfig = 2.0; 
int minPozoConfig = 20;    

unsigned long ultimaComunicacion = 0;
const unsigned long TIMEOUT_LORA = 30000;      
const unsigned long MEDICION_INTERVAL = 3000;   


void guardarConfig() {
    prefs.putFloat("profPozo", profPozoConfig);
    prefs.putInt("minPozo", minPozoConfig);
}




void medirNivel() {
    digitalWrite(TRIG_POZO, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_POZO, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_POZO, LOW);

    long duracion = pulseIn(ECHO_POZO, HIGH, 30000);
    if (duracion <= 0) return;

    float distancia = (duracion * 0.034) / 2.0;        
    float alturaMax = profPozoConfig * 100.0;          
    float alturaAgua = alturaMax - distancia;

    if (alturaAgua < 0) alturaAgua = 0;
    if (alturaAgua > alturaMax) alturaAgua = alturaMax;

    nivelPozo = (alturaAgua / alturaMax) * 100.0;
    nivelPozo = constrain(nivelPozo, 0, 100);
}


void enviarEstado() {
    LoRa.beginPacket();
    LoRa.print("{\"p\":");
    LoRa.print(nivelPozo);
    LoRa.print(",\"b\":");
    LoRa.print(bombaEstado ? 1 : 0);
    LoRa.print("}");
    LoRa.endPacket();
    Serial.printf("→ Estado enviado: Nivel=%d%% | Bomba=%s\n", nivelPozo, bombaEstado ? "ON" : "OFF");
}

void procesarConfiguracion(String json) {
    int posMin = json.indexOf("\"min\":");
    int posProf = json.indexOf("\"prof\":");
    if (posMin != -1) minPozoConfig = json.substring(posMin + 6).toInt();
    if (posProf != -1) profPozoConfig = json.substring(posProf + 7).toFloat();
    guardarConfig();
    enviarEstado();
    Serial.println("✔ Configuración actualizada");
}


void setup() {
    Serial.begin(115200);
    Serial.println("\n=== NODO POZO INICIANDO ===");

    prefs.begin("pozo", false);
    profPozoConfig = prefs.getFloat("profPozo", 2.0);
    minPozoConfig = prefs.getInt("minPozo", 20);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);   // bomba OFF
    pinMode(TRIG_POZO, OUTPUT);
    pinMode(ECHO_POZO, INPUT);

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(433E6)) {
        Serial.println("ERROR LoRa");
        while (1);
    }
    LoRa.setSyncWord(0xF3);
    LoRa.enableCrc();

    ultimaComunicacion = millis();
    medirNivel();
}


void loop() {
    static unsigned long lastMed = 0;
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String msg = "";
        while (LoRa.available()) {
            char c = LoRa.read();
            if (c >= 32 && c <= 126) msg += c;
        }

        ultimaComunicacion = millis();
        Serial.println("Mensaje recibido: " + msg);

        if (msg.indexOf("\"cmd\"") != -1) {
            if (msg.indexOf("ON") != -1 && nivelPozo >= minPozoConfig) {
                bombaEstado = true;
                digitalWrite(RELAY_PIN, LOW);
            }
            else if (msg.indexOf("OFF") != -1) {
                bombaEstado = false;
                digitalWrite(RELAY_PIN, HIGH);
            }
            enviarEstado(); // Siempre responder al comando
        }
        else if (msg.indexOf("\"min\"") != -1 || msg.indexOf("\"prof\"") != -1) {
            procesarConfiguracion(msg);
        }
        else {
            enviarEstado();
        }
    }


    if (bombaEstado && millis() - ultimaComunicacion > TIMEOUT_LORA) {
        bombaEstado = false;
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("Timeout maestro → Bomba OFF");
    }
    
    if (millis() - lastMed > MEDICION_INTERVAL) {
        medirNivel();
        lastMed = millis();

        if (bombaEstado && nivelPozo < minPozoConfig) {
            bombaEstado = false;
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println(" Nivel bajo → Bomba OFF");

        }
    }

    delay(10);
}
