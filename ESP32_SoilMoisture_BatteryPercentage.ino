#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESP_Google_Sheet_Client.h>
#include "time.h"
#include <Adafruit_ADS1X15.h>
#include <GSheet32.h>

//Credenciales del WiFi
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

//Credenciales para comunicacion con google sheets
#define PROJECT_ID ""
#define CLIENT_EMAIL ""
const char PRIVATE_KEY[] PROGMEM = "";
const char spreadsheetId[] = "";

// Objeto ADS1115
Adafruit_ADS1115 ads;

// Valor de las resistencias para el divisor de voltajes
const float R1 = 200000.0;  // resistencia de 200kΩ
const float R2 = 200000.0;  // resistencia de 200kΩ 
const float batteryMaxVoltage = 3.7;  // Voltaje maximo de la bateria
const float batteryMinVoltage = 3.2;  // Voltaje minimo de la bateria
const float soilMoistureMaxVoltage = 1.5;  // Voltaje maximo del sensor SM-100
const float soilMoistureMinVoltage = 0.85;  // Voltaje minimo del sensor SM-100
const int indicatorPin = 5; //Pin 5 para colocar en HIGH o LOW

// Servidor NTP para solicitar la hora
const char* ntpServer = "time.google.com";
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;

void tokenStatusCallback(TokenInfo info);
 
String getFormattedTime() {
  struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(timeStr);
    } else {
        return "Time unavailable";
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(indicatorPin, OUTPUT); 
  digitalWrite(indicatorPin, HIGH); //Pin 5 en alto

  // Conexion a la red Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  //zona horaria de Costa Rica
  configTime(-21600, 0, ntpServer);        

  // Syncronizacion de la hora
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
      GSheet.setSystemTime(mktime(&timeinfo));
      Serial.println("Time synchronized!");
  } else {
      Serial.println("Failed to synchronize time.");
  }  

   if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115.");
    while (1); 
  } else {
    Serial.println("ADS1115 initialized.");
  }

  // Ganancia del ADS1115
  ads.setGain(GAIN_TWO);
  
  GSheet.setTokenCallback(tokenStatusCallback);
  GSheet.setPrerefreshSeconds(10 * 60);
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
}

void loop() {
  if (GSheet.ready() && millis() - lastTime > timerDelay) {
    lastTime = millis();

    //Variable de Fecha-Hora
    String timeString = getFormattedTime();

    //Lectura del voltaje del sensor SM-100
    int16_t soilMoistureReading = ads.readADC_SingleEnded(0);
    float soilMoistureVoltage = ads.computeVolts(soilMoistureReading);

    
    //Lectura del voltaje de la bateria utilizando el modo diferencial utilizando los pines A2-A3
    int16_t batteryReading = ads.readADC_Differential_2_3();
    float batteryVoltage = ads.computeVolts(batteryReading);

    // Calculo del voltaje de la bateria usando divisor de voltaje
    float actualBatteryVoltage = batteryVoltage * ((R1 + R2) / R2);

    // Conversion de humedad del suelo a porcentaje (0 to 100%)
    float soilMoisturePercentage = (soilMoistureVoltage - soilMoistureMinVoltage) / (soilMoistureMaxVoltage - soilMoistureMinVoltage) * 100.0;


    //Conversión del voltaje de la batería a porcentaje con respecto al voltaje máximo y mínimo de la batería
    float batteryPercentage = ((actualBatteryVoltage - batteryMinVoltage) / (batteryMaxVoltage - batteryMinVoltage)) * 100.0;

    //Validación de porcentajes de humedad del suelo y batería
    if (batteryPercentage > 100.0) {
      batteryPercentage = 100.0;
    } else if (batteryPercentage < 0) {
      batteryPercentage = 0.0;
    }

    if (soilMoistureVoltage > 1.5) {
      soilMoisturePercentage = 100.0;
    } else if (soilMoistureVoltage < 0.85) {
      soilMoisturePercentage = 0.0;
    }


    // Preparación de datos para ser enviados a Google Sheets
    FirebaseJson response;
    FirebaseJson valueRange;
    valueRange.add("majorDimension", "ROWS");
    valueRange.set("values/[1]/[0]", timeString);
    valueRange.set("values/[1]/[1]", soilMoisturePercentage);
    valueRange.set("values/[1]/[2]", batteryPercentage);

    const int maxRetries = 3;  // Numero maximo de intentos
    int retryCount = 0;
    bool success = false;
    // Comprueba la conexión con Google Sheets
    do {
      success = GSheet.values.append(&response, spreadsheetId, "Humedad-Planta-1!A2", &valueRange);

      if (success) {
        response.toString(Serial, true);
        valueRange.clear();
      } else {
        Serial.print("Failed to send data. Attempt ");
        Serial.print(retryCount + 1);
        Serial.print(" of ");
        Serial.println(maxRetries);
        Serial.println(GSheet.errorReason());

        delay(2000); // 2 seconds
      }

      retryCount++;
    } while (!success && retryCount < maxRetries);

    if (!success) {
      Serial.println("Data transmission failed after maximum retries.");
    }

    // Impresión de los valores en el monitor serial
    Serial.print("Soil Moisture: ");
    Serial.print(soilMoisturePercentage);
    Serial.println("%");

    Serial.print("SM Voltage: ");
    Serial.println(soilMoistureVoltage);

    Serial.print("SM ADC: ");
    Serial.println(soilMoistureReading);

    Serial.print("Battery: ");
    Serial.print(batteryPercentage);
    Serial.println("%");

    Serial.print("Battery Voltage: ");
    Serial.print(actualBatteryVoltage);
    Serial.println("V");
  

    Serial.println();
    digitalWrite(indicatorPin, LOW); //Pin 5 en LOW
    // Inicio de estado deep sleep
    Serial.println("Entering deep sleep for 30 minutes...");
    esp_sleep_enable_timer_wakeup(1800 * 1000000);  // 30 minutos
    esp_deep_sleep_start();
  }
}


void tokenStatusCallback(TokenInfo info) {
  // Verifica si el estado del token es un error
  if (info.status == token_status_error) {
    GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
    GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
  } else {
    // Imprime el tipo y estado del token, y el error
    GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
  }
}