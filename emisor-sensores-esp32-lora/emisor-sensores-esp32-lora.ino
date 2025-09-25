// Turns the 'PRG' button into the power button, long press is off 
#define HELTEC_POWER_BUTTON   // must be before "#include <heltec_unofficial.h>"
#include <heltec_unofficial.h>
#include "DHT.h"              // Librería para el sensor AM2301

// --- Configuración del Sensor DHT ---
#define DHTPIN 19      // Pin GPIO donde está conectado el cable de datos (Amarillo)
#define DHTTYPE DHT21  // El sensor AM2301 es equivalente al DHT21 para esta librería

// --- Configuración del Sensor MQ-135 ---
// CAMBIADO: Usar pin analógico válido para ESP32
#define MQ135_ANALOG_PIN 1     // ADC1_CH0 - Pin analógico válido
#define MQ135_DIGITAL_PIN 2    // Pin digital para umbral (opcional)

// Parámetros de calibración del MQ-135
#define RL_VALUE 10.0          // Resistencia de carga en kOhms
#define RO_CLEAN_AIR_FACTOR 3.6 // Factor de resistencia en aire limpio
#define CALIBRATION_SAMPLE_TIMES 20    // REDUCIDO: Menos muestras para evitar timeouts
#define CALIBRATION_SAMPLE_INTERVAL 200   // REDUCIDO: Intervalo más corto
#define READ_SAMPLE_INTERVAL 100       // AUMENTADO: Más tiempo entre lecturas
#define READ_SAMPLE_TIMES 3           // REDUCIDO: Menos muestras por lectura

// Inicializa el sensor DHT.
DHT dht(DHTPIN, DHTTYPE);

// --- Configuración LoRa ---
#define FREQUENCY           905.2       // para Europa
#define BANDWIDTH           250.0
#define SPREADING_FACTOR    12
#define TRANSMIT_POWER      22

long counter = 0;
uint64_t last_tx = 0;
uint64_t tx_time;
uint64_t minimum_pause;
uint64_t last_sensor_read = 0;
float ultima_temperatura = 0.0;
float ultima_humedad = 0.0;
float ultimo_co2_ppm = 0.0;
float ultimo_co_ppm = 0.0;
float Ro = 10.0; // Resistencia del sensor en aire limpio

// Variable para controlar errores I2C
bool display_error = false;

void setup() {
  heltec_setup();
  both.println("=== SENSOR DHT + MQ-135 + LoRa Heltec v3 ===");
  both.println("Inicializando radio...");
  RADIOLIB_OR_HALT(radio.begin());
  
  // Configurar parámetros del radio
  both.printf("Frecuencia: %.2f MHz\n", FREQUENCY);
  RADIOLIB_OR_HALT(radio.setFrequency(FREQUENCY));
  both.printf("Ancho de banda: %.1f kHz\n", BANDWIDTH);
  RADIOLIB_OR_HALT(radio.setBandwidth(BANDWIDTH));
  both.printf("Factor de dispersión: %i\n", SPREADING_FACTOR);
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(SPREADING_FACTOR));
  both.printf("Potencia TX: %i dBm\n", TRANSMIT_POWER);
  RADIOLIB_OR_HALT(radio.setOutputPower(TRANSMIT_POWER));
  
  // Configurar pines MQ-135
  pinMode(MQ135_ANALOG_PIN, INPUT);
  pinMode(MQ135_DIGITAL_PIN, INPUT);
  
  // Configurar resolución del ADC
  analogReadResolution(12); // 12 bits de resolución
  analogSetAttenuation(ADC_11db); // Para leer hasta 3.3V
  
  // Inicializar sensores
  dht.begin();
  both.println("Sensor DHT inicializado");
  
  // Mostrar mensaje de calibración
  both.println("Calibrando MQ-135 en aire limpio...");
  mostrarEnPantallaSafe("Calibrando MQ-135", "Espere...", "", "", "");
  
  // Calibrar MQ-135 con manejo de errores
  Ro = calibrarMQ135();
  both.printf("Calibración completada. Ro = %.2f kOhms\n", Ro);
  
  // Mostrar mensaje inicial
  mostrarEnPantallaSafe("Sistema listo", "", "", "", "");
  delay(2000);
  both.println("Sistema listo para funcionar");
}

void loop() {
  heltec_loop();
  
  bool tx_legal = millis() > last_tx + minimum_pause;
  
  // Leer sensores cada 10 segundos (AUMENTADO para reducir carga I2C)
  if (millis() - last_sensor_read > 1000) {
    leerSensores();
    last_sensor_read = millis();
  }
  
  // Transmitir datos cada 60 segundos o cuando se presione el botón
  if ((tx_legal && millis() - last_tx > 1000) || button.isSingleClick()) {
    if (!tx_legal) {
      both.printf("Límite legal, espera %i seg.\n", (int)((minimum_pause - (millis() - last_tx)) / 1000) + 1);
      return;
    }
    
    transmitirDatos();
  }
}

void leerSensores() {
  // Leer DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  // Verificar lecturas DHT
  if (isnan(h) || isnan(t)) {
    both.println("Error al leer el sensor DHT");
    h = ultima_humedad; // Usar último valor válido
    t = ultima_temperatura;
  }
  
  // Leer MQ-135 con manejo de errores
  float mq_resistance = 0;
  bool mq_ok = false;
  
  // Intentar leer MQ-135 con timeout
  unsigned long start_time = millis();
  while (millis() - start_time < 2000) { // Timeout de 2 segundos
    mq_resistance = leerMQ135();
    if (mq_resistance > 0) {
      mq_ok = true;
      break;
    }
    delay(100);
  }
  
  float co2_ppm = 0;
  float co_ppm = 0;
  
  if (mq_ok) {
    co2_ppm = leerCO2(mq_resistance);
    co_ppm = leerCO(mq_resistance);
  } else {
    both.println("Error al leer MQ-135, usando valores anteriores");
    co2_ppm = ultimo_co2_ppm;
    co_ppm = ultimo_co_ppm;
  }
  
  // Actualizar variables globales
  ultima_temperatura = t;
  ultima_humedad = h;
  ultimo_co2_ppm = co2_ppm;
  ultimo_co_ppm = co_ppm;
  
  // Mostrar en pantalla con manejo seguro
  mostrarDatosEnPantalla();
  
  // Mostrar en serial
  both.printf("Temp: %.1f°C, Humedad: %.1f%%, CO2: %.0f ppm, CO: %.1f ppm\n", 
              t, h, co2_ppm, co_ppm);
}

// Función segura para mostrar datos en pantalla
void mostrarEnPantallaSafe(String linea1, String linea2, String linea3, String linea4, String linea5) {
  // Reintentar varias veces si hay error I2C
  for (int retry = 0; retry < 3; retry++) {
    try {
      display.clear();
      display.setFont(ArialMT_Plain_10);
      
      if (linea1.length() > 0) display.drawString(0, 0, linea1);
      if (linea2.length() > 0) display.drawString(0, 12, linea2);
      if (linea3.length() > 0) display.drawString(0, 24, linea3);
      if (linea4.length() > 0) display.drawString(0, 36, linea4);
      if (linea5.length() > 0) display.drawString(0, 48, linea5);
      
      display.display();
      display_error = false;
      return; // Éxito, salir
    } catch (...) {
      display_error = true;
      delay(100); // Esperar antes del siguiente intento
    }
  }
  
  // Si llegamos aquí, hay un error persistente de I2C
  both.println("Error persistente en pantalla I2C");
}

void mostrarDatosEnPantalla() {
  String linea1 = "T:" + String(ultima_temperatura, 1) + "C H:" + String(ultima_humedad, 0) + "%";
  String linea2 = "CO2: " + String(ultimo_co2_ppm, 0) + " ppm";
  String linea3 = "CO: " + String(ultimo_co_ppm, 1) + " ppm";
  String linea4 = "Paquetes: " + String(counter);
  String linea5 = "LoRa: " + String(FREQUENCY, 1) + " MHz";
  
  mostrarEnPantallaSafe(linea1, linea2, linea3, linea4, linea5);
}

void transmitirDatos() {
  // Crear mensaje con todos los datos
  String mensaje = "|TEMP:" + String(ultima_temperatura, 1) + 
                   "|HUM:" + String(ultima_humedad, 1) +
                   "|CO2:" + String(ultimo_co2_ppm, 0) +
                   "|CO:" + String(ultimo_co_ppm, 1) +
                   "|CNT:" + String(counter);
  
  heltec_led(50);
  tx_time = millis();
  RADIOLIB(radio.transmit(mensaje.c_str()));
  tx_time = millis() - tx_time;
  heltec_led(0);
  
  if (_radiolib_status == RADIOLIB_ERR_NONE) {
    both.printf("TX OK (%i ms): %s\n", (int)tx_time, mensaje.c_str());
    
    // Actualizar pantalla con estado de transmisión
    String linea1 = "TX OK #" + String(counter);
    String linea2 = "Tiempo: " + String(tx_time) + " ms";
    String linea3 = "CO2: " + String(ultimo_co2_ppm, 0) + " ppm";
    String linea4 = "Temp: " + String(ultima_temperatura, 1) + "C";
    String linea5 = "";
    
    mostrarEnPantallaSafe(linea1, linea2, linea3, linea4, linea5);
    
  } else {
    both.printf("Error TX: %i\n", _radiolib_status);
    mostrarEnPantallaSafe("Error TX!", "Codigo: " + String(_radiolib_status), "", "", "");
  }
  
  counter++;
  minimum_pause = tx_time * 1; // 1% duty cycle
  last_tx = millis();
}

// ===== FUNCIONES PARA MQ-135 =====

float calibrarMQ135() {
  float val = 0;
  int muestras_validas = 0;
  
  for (int i = 0; i < CALIBRATION_SAMPLE_TIMES; i++) {
    float lectura = leerMQ135();
    if (lectura > 0) { // Solo contar lecturas válidas
      val += lectura;
      muestras_validas++;
    }
    delay(CALIBRATION_SAMPLE_INTERVAL);
    
    // Mostrar progreso cada 5 muestras
    if (i % 5 == 0) {
      both.printf("Calibrando... %d/%d\n", i, CALIBRATION_SAMPLE_TIMES);
    }
  }
  
  if (muestras_validas > 0) {
    val = val / muestras_validas;
    val = val / RO_CLEAN_AIR_FACTOR;
    return val;
  } else {
    both.println("Error en calibración, usando valor por defecto");
    return 10.0; // Valor por defecto
  }
}

float leerMQ135() {
  float rs = 0;
  int lecturas_validas = 0;
  
  for (int i = 0; i < READ_SAMPLE_TIMES; i++) {
    int raw_adc = analogRead(MQ135_ANALOG_PIN);
    
    // Verificar que la lectura sea válida
    if (raw_adc > 0 && raw_adc < 4095) {
      float resistencia = calcularResistencia(raw_adc);
      if (resistencia > 0) {
        rs += resistencia;
        lecturas_validas++;
      }
    }
    delay(READ_SAMPLE_INTERVAL);
  }
  
  if (lecturas_validas > 0) {
    return rs / lecturas_validas;
  } else {
    return 0; // Indica error
  }
}

float calcularResistencia(int raw_adc) {
  if (raw_adc <= 0 || raw_adc >= 4095) {
    return 0; // Valor inválido
  }
  return (((float)RL_VALUE * (4095 - raw_adc)) / raw_adc);
}

float leerCO2(float rs_gas) {
  if (Ro <= 0 || rs_gas <= 0) return 0;
  
  float ratio = rs_gas / Ro;
  if (ratio <= 0) return 0;
  
  float ppm = 116.6020682 * pow(ratio, -2.769034857);
  
  // Limitar valores extremos
  if (ppm < 350) ppm = 350;   // CO2 mínimo atmosférico
  if (ppm > 10000) ppm = 10000; // Límite máximo razonable
  
  return ppm;
}

float leerCO(float rs_gas) {
  if (Ro <= 0 || rs_gas <= 0) return 0;
  
  float ratio = rs_gas / Ro;
  if (ratio <= 0) return 0;
  
  float ppm = 605.18 * pow(ratio, -3.937);
  
  // Limitar valores extremos
  if (ppm < 0) ppm = 0;
  if (ppm > 1000) ppm = 1000; // Límite máximo razonable para CO
  
  return ppm;
}