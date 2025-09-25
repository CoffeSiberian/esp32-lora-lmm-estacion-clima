// Turns the 'PRG' button into the power button, long press is off 
#define HELTEC_POWER_BUTTON   // must be before "#include <heltec_unofficial.h>"
#include <heltec_unofficial.h>

// IMPORTANTE: Usar exactamente la misma configuración que el emisor
// #define FREQUENCY           866.3       // para Europa
#define FREQUENCY           905.2       // para US/América
#define BANDWIDTH           250.0
#define SPREADING_FACTOR    12

String rxdata;
volatile bool rxFlag = false;

void setup() {
  heltec_setup();

  heltec_ve(true);
  both.println("=== RECEPTOR LoRa Heltec v3 ===");
  both.println("Inicializando radio...");
  RADIOLIB_OR_HALT(radio.begin());
  
  // Configurar la función de callback para paquetes recibidos
  radio.setDio1Action(rx);
  
  // Configurar parámetros del radio (iguales al emisor)
  both.printf("Frecuencia: %.2f MHz\n", FREQUENCY);
  RADIOLIB_OR_HALT(radio.setFrequency(FREQUENCY));
  both.printf("Ancho de banda: %.1f kHz\n", BANDWIDTH);
  RADIOLIB_OR_HALT(radio.setBandwidth(BANDWIDTH));
  both.printf("Factor de dispersión: %i\n", SPREADING_FACTOR);
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(SPREADING_FACTOR));
  
  // Comenzar a recibir
  RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  both.println("RECEPTOR esperando datos...");
}

void loop() {
  heltec_loop();
  
  // Si se recibió un paquete, mostrarlo junto con RSSI y SNR
  if (rxFlag) {
    rxFlag = false;
    radio.readData(rxdata);
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      heltec_led(50); // 50% de brillo es suficiente para este LED
      both.printf("RX [%s]\n", rxdata.c_str());
      heltec_led(0);
      both.printf("  RSSI: %.2f dBm\n", radio.getRSSI());
      both.printf("  SNR: %.2f dB\n", radio.getSNR());
      both.println("---");
    }
    RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  }
}

// No se puede hacer Serial o display aquí, toma demasiado tiempo para la interrupción
void rx() {
  rxFlag = true;
}