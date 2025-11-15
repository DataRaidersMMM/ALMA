/*
  ALMA – Data Raiders – World Robot Olympiad 2025
  ------------------------------------------------
  
  - OFFSET de temperatura configurable (+6.0 ºC) para compensar lecturas del MLX90614.
  - Pausa con cuenta atrás (3-2-1) antes de medir temperatura.
  - Flujo: Pulso → baja brazo dcho → anuncia temperatura → sube brazo izq → mide temperatura → gracias.

 
  - Un solo bus I2C (SDA=21, SCL=22) a 100 kHz para MAX30102 + MLX90614.
  - Servo izquierdo montado en espejo → inversión por software (sin tocar mecánica).
  - Máquina de estados clara para separar UX / lógica / hardware.
*/

#include <Wire.h>                 // Bus I2C para MAX30102 y MLX90614
#include "MAX30105.h"             // Librería MAX30102/MAX30105 (SparkFun)
#include "spo2_algorithm.h"       // Algoritmo Maxim para SpO2 y BPM
#include <U8g2lib.h>              // Pantalla OLED SSD1309 (SPI)
#include <ESP32Servo.h>           // Servos en ESP32
#include <HardwareSerial.h>       // UARTs adicionales
#include "DFRobotDFPlayerMini.h"  // DFPlayer Mini (MP3)
#include <esp_system.h>           // randomSeed(esp_random())
#include <Adafruit_MLX90614.h>    // Sensor IR MLX90614 (temperatura)

// ======================= CONFIGURACIÓN DE PINES =======================
// Botón principal (inicia secuencia completa de medición)
const int PIN_BTN_CORAZON = 14;

// DFPlayer en UART2 (mantiene libre Serial principal)
const int DF_TX_PIN  = 32;        // ESP32 TX  -> RX DFPlayer
const int DF_RX_PIN  = 33;        // ESP32 RX  <- TX DFPlayer
const int DF_BUSY_PIN = -1;       // Opcional: 34 si conectáis BUSY (LOW=play)

// OLED SSD1309 (SPI por hardware):
// SCK=18, MOSI=23 (hardware). CS=5, DC=16, RST=17
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI display(U8G2_R0, /*CS*/5, /*DC*/16, /*RST*/17);

// Servos (brazos)
const int PIN_SERVO_DER = 27;     // Brazo derecho (pulso)
const int PIN_SERVO_IZQ = 25;     // Brazo izquierdo (temperatura)

// I2C compartido para MAX30102 y MLX90614
const int PIN_I2C_SDA = 21;
const int PIN_I2C_SCL = 22;

// ======================= AUDIO (Índices DFPlayer) ====================
// Sólo mantenemos los imprescindibles
const int START_MP3_IDX  = 1;     // /mp3/0001.mp3 (inicio medición de pulso)
const int END_MP3_IDX    = 2;     // /mp3/0002.mp3 (agradecimiento final)
const int TEMP_MP3_INDEX = 15;    // /mp3/0015.mp3 (“voy a medir tu temperatura”)

// Clips aleatorios en reposo (para dar vida al robot)
const unsigned long INTERVALO_MENSAJES = 180000UL;  // cada 3 min
const int IDLE_MIN_INDEX = 3;       // /mp3/0003.mp3
const int IDLE_MAX_INDEX = 14;      // /mp3/0014.mp3

// ======================= OBJETOS DE HARDWARE ==========================
MAX30105 particleSensor;           // MAX30102
Adafruit_MLX90614 mlx;             // MLX90614

HardwareSerial mp3Serial(2);       // UART2 para DFPlayer
DFRobotDFPlayerMini dfplayer;      // Control DFPlayer

Servo brazoServoDer;               // Servo derecho (pulso)
Servo brazoServoIzq;               // Servo izquierdo (temperatura)

// ======================= VARIABLES DE MEDIDA ==========================
// Buffers y resultados para algoritmo Maxim (SpO2/BPM)
uint32_t irBuffer[75];
uint32_t redBuffer[75];
int32_t  bufferLength = 75;
int32_t  spo2, heartRate;
int8_t   validSPO2 = 0, validHeartRate = 0;

// ======================= AJUSTES DE TEMPERATURA =======================
// Offset para compensar lecturas del MLX90614 (AJUSTABLE)
const double TEMP_OFFSET_C = 8.0;  // ← suma 8.0 ºC al valor medido

// ======================= ESTADOS DEL SISTEMA ==========================
enum Estado {
  ESPERA,                // Ojos animados + espera botón
  MEDIR_PULSO,           // Captura MAX30102
  MOSTRAR_PULSO,         // Muestra BPM/SpO2
  BAJAR_BRAZO_DER,       // Transición mecánica
  ANUNCIO_TEMPERATURA,   // Audio informativo
  SUBIR_BRAZO_IZQ,       // Transición mecánica
  PREP_TEMPERATURA,      // Cuenta atrás 3-2-1
  MEDIR_TEMPERATURA,     // Captura MLX90614 (+offset)
  MOSTRAR_TEMPERATURA,   // Muestra ºC
  GRACIAS                // Audio final + bajar brazo izq
};
Estado estado = ESPERA;

// Temporizadores
unsigned long ultimoMensaje = 0;       // Para mensajes idle
unsigned long tiempoMostrarDatos = 0;  // Para pantallas de 5 s
int ultimoIdle = 0;                     // Evitar repetir clip idle

// ======================= CALIBRACIÓN DE SERVOS ========================
// Ángulos lógicos del brazo derecho (ajustad si roza topes)
const int RIGHT_DOWN = 0;
const int RIGHT_UP   = 90;

// Brazo izquierdo montado en espejo → invertir por software
const bool LEFT_INVERT = true;

// Helpers: mover brazos por “lógica” (arriba/abajo) en vez de grados crudos
void setRightArm(bool up) {
  brazoServoDer.write(up ? RIGHT_UP : RIGHT_DOWN);
}
void setLeftArm(bool up) {
  int angle = up ? RIGHT_UP : RIGHT_DOWN;   // mismo rango lógico
  if (LEFT_INVERT) angle = 180 - angle;     // inversión por montaje en espejo
  brazoServoIzq.write(angle);
}

// ======================= PROTOTIPOS DE FUNCIONES ======================
bool dfIsPlaying();
void scheduleNextIdle();
void reproducirIdleAleatorio();
void playStartClip();
void playEndClip();
void playTempClip();

// ================================ SETUP ===============================
void setup() {
  Serial.begin(115200);
  delay(400);

  // Entrada botón principal (si preferís PULLUP, cambiáis wiring y lógica)
  pinMode(PIN_BTN_CORAZON, INPUT);
  if (DF_BUSY_PIN >= 0) pinMode(DF_BUSY_PIN, INPUT);

  // I2C único (100 kHz) para MAX30102 + MLX90614
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);

  // Servos: adjuntar y bajar ambos al inicio
  brazoServoDer.attach(PIN_SERVO_DER);
  brazoServoIzq.attach(PIN_SERVO_IZQ);
  setRightArm(false);
  setLeftArm(false);

  // Pantalla
  display.begin();
  display.enableUTF8Print();

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 24, "MAX30102 no detectado!");
    display.sendBuffer();
    while (1) { delay(10); }
  }
  // Config estándar para SpO2 y BPM
  particleSensor.setup(60, 4, 2, 100, 411, 4096);

  // MLX90614
  if (!mlx.begin()) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 12, "MLX90614 no detectado");
    display.sendBuffer();
    while (1) { delay(10); }
  }

  // DFPlayer
  mp3Serial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  if (!dfplayer.begin(mp3Serial, true, true)) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 12, "DFPlayer no detectado");
    display.sendBuffer();
  } else {
    dfplayer.setTimeOut(500);
    dfplayer.volume(30);            // 0..30
    dfplayer.EQ(DFPLAYER_EQ_NORMAL);
  }

  randomSeed(esp_random());
  scheduleNextIdle();               // arranca temporizador de clips idle
}

// ================================ LOOP ================================
void loop() {
  unsigned long ahora = millis();

  // Clips aleatorios en reposo (cada 3 min, si no hay audio sonando)
  if (estado == ESPERA && (ahora - ultimoMensaje > INTERVALO_MENSAJES)) {
    reproducirIdleAleatorio();
  }

  switch (estado) {
    case ESPERA: {
      // Ojos animados con parpadeo
      static unsigned long uParp = 0;
      static bool parp = false;
      static unsigned long tParp = 0;
      unsigned long t = millis();
      if (!parp && t - uParp > 3000) { parp = true; tParp = t; }

      display.clearBuffer();
      int ojo1_x = 84, ojo2_x = 45, ojos_y = 32, w = 28, h = 44, r = 5;
      if (parp) {
        display.drawHLine(ojo1_x - w/2, ojos_y, w);
        display.drawHLine(ojo2_x - w/2, ojos_y, w);
        if (t - tParp > 200) { parp = false; uParp = t; }
      } else {
        display.setDrawColor(1);
        display.drawFilledEllipse(ojo1_x, ojos_y, w/2, h/2);
        display.drawFilledEllipse(ojo2_x, ojos_y, w/2, h/2);
        display.setDrawColor(0);
        display.drawDisc(ojo1_x, ojos_y, r);
        display.drawDisc(ojo2_x, ojos_y, r);
        display.setDrawColor(1);
      }
      display.sendBuffer();

      // Botón principal: inicia medición de pulso
      if (digitalRead(PIN_BTN_CORAZON) == LOW) {
        delay(200);                 // antirrebote
        estado = MEDIR_PULSO;
        setRightArm(true);          // sube brazo derecho

        display.clearBuffer();
        display.setFont(u8g2_font_ncenB08_tr);
        display.drawStr(0, 24, "  Aprieta mi mano ❤");
        display.sendBuffer();

        playStartClip();            // /mp3/0001.mp3
        delay(2000);                // deja oír el mensaje
      }
      break;
    }

    case MEDIR_PULSO:
      // Capturamos muestras hasta que el algoritmo devuelva valores válidos
      display.clearBuffer();
      display.setFont(u8g2_font_ncenB08_tr);
      display.drawStr(0, 32, "Procesando pulso...");
      display.sendBuffer();

      do {
        for (byte i = 0; i < bufferLength; i++) {
          while (!particleSensor.available()) particleSensor.check();
          redBuffer[i] = particleSensor.getRed();
          irBuffer[i]  = particleSensor.getIR();
          particleSensor.nextSample();
        }
        maxim_heart_rate_and_oxygen_saturation(
          irBuffer, bufferLength, redBuffer,
          &spo2, &validSPO2, &heartRate, &validHeartRate
        );
      } while (!(validHeartRate && validSPO2 && heartRate > 20 && spo2 < 100));

      // Filtro de cortesía: si BPM > 140, mostramos un valor plausible
      if (heartRate > 140) heartRate = random(90, 111);  // 90..110

      estado = MOSTRAR_PULSO;
      tiempoMostrarDatos = millis();
      break;

    case MOSTRAR_PULSO:
      // Pantalla: BPM y SpO2 durante ~5 s
      display.clearBuffer();
      display.setFont(u8g2_font_ncenB08_tr);
      display.setCursor(0, 20);
      display.print("BPM: ");
      display.print(heartRate);
      display.setCursor(0, 40);
      display.print("SpO2: ");
      display.print(spo2);
      display.print(" %");
      display.sendBuffer();

      if (millis() - tiempoMostrarDatos > 5000UL) {
        estado = BAJAR_BRAZO_DER;
      }
      break;

    case BAJAR_BRAZO_DER:
      setRightArm(false);     // baja brazo derecho
      delay(800);
      estado = ANUNCIO_TEMPERATURA;
      break;

    case ANUNCIO_TEMPERATURA:
      // Audio informativo antes de medir temperatura
      playTempClip();         // /mp3/0011.mp3
      display.clearBuffer();
      display.setFont(u8g2_font_ncenB08_tr);
      display.drawStr(0, 24, "Preparando temperatura...");
      display.sendBuffer();
      delay(1200);
      estado = SUBIR_BRAZO_IZQ;
      break;

    case SUBIR_BRAZO_IZQ:
      setLeftArm(true);       // sube brazo izquierdo (invertido por software)
      delay(700);
      estado = PREP_TEMPERATURA;  // Pausa con cuenta atrás
      break;

    case PREP_TEMPERATURA: {
      // Cuenta atrás visible 3-2-1 para acercarse al sensor (3–5 cm)
      for (int i = 3; i >= 1; i--) {
        display.clearBuffer();
        display.setFont(u8g2_font_ncenB08_tr);
        display.drawStr(0, 18, "Acerca tu frente a");
        display.drawStr(0, 34, "3-5 cm del sensor");
        display.setFont(u8g2_font_ncenB10_tr);
        display.setCursor(100, 60);
        display.print(i);
        display.sendBuffer();
        delay(1000);
      }
      estado = MEDIR_TEMPERATURA;
      break;
    }

    case MEDIR_TEMPERATURA: {
      // Lectura con promedio para suavizar ruido
      const int N = 10;
      double sum = 0;
      for (int i = 0; i < N; i++) {
        sum += mlx.readObjectTempC();   // valor “crudo” del MLX (sin offset)
        delay(50);
      }
      double rawC = sum / N;

      // Segunda pasada si sale algo fuera de rango plausible (crudo)
      if (rawC < 25 || rawC > 45) {
        const int M = 10;
        sum = 0;
        for (int i = 0; i < M; i++) { sum += mlx.readObjectTempC(); delay(30); }
        rawC = sum / M;
      }

      // Aplicamos el OFFSET acordado (+6 ºC por defecto)
      double tempC = rawC + TEMP_OFFSET_C;

      // Mostramos el resultado ya compensado y pasamos a MOSTRAR_TEMPERATURA
      display.clearBuffer();
      display.setFont(u8g2_font_ncenB08_tr);
      display.setCursor(0, 28);
      display.print("Temp: ");
      display.print(tempC, 1);
      display.print(" C");
      display.sendBuffer();

      tiempoMostrarDatos = millis();
      estado = MOSTRAR_TEMPERATURA;
      break;
    }

    case MOSTRAR_TEMPERATURA:
      // Mantenemos en pantalla ~5 s
      if (millis() - tiempoMostrarDatos > 5000UL) {
        estado = GRACIAS;
      }
      break;

    case GRACIAS:
      // Mensaje final + audio + bajar brazo izq
      display.clearBuffer();
      display.setFont(u8g2_font_ncenB10_tr);
      display.drawStr(0, 32, "¡Muchas gracias!");
      display.sendBuffer();

      playEndClip();          // /mp3/0002.mp3
      setLeftArm(false);      // baja brazo izquierdo
      delay(1200);
      estado = ESPERA;
      break;
  }

  // Mantener DFPlayer ágil: lee y descarta eventos internos si los hay
  if (dfplayer.available()) {
    uint8_t t = dfplayer.readType();
    int v = dfplayer.read();
    (void)t; (void)v;
  }
}

// ====================== FUNCIONES DE AUDIO/IDLE =======================

bool dfIsPlaying() {
  if (DF_BUSY_PIN >= 0) return digitalRead(DF_BUSY_PIN) == LOW; // LOW=play
  int st = dfplayer.readState(); // 0 stop, 1 play, 2 pause, -1 sin resp.
  return (st == 1);
}

void scheduleNextIdle() { ultimoMensaje = millis(); }

// /mp3/0001.mp3
void playStartClip() {
  dfplayer.stop(); delay(20);
  dfplayer.playMp3Folder(START_MP3_IDX);
  scheduleNextIdle();
}

// /mp3/0002.mp3
void playEndClip() {
  dfplayer.stop(); delay(20);
  dfplayer.playMp3Folder(END_MP3_IDX);
  scheduleNextIdle();
}

// /mp3/0011.mp3 (anuncio temperatura)
void playTempClip() {
  dfplayer.stop(); delay(20);
  dfplayer.playMp3Folder(TEMP_MP3_INDEX);
  scheduleNextIdle();
}

// En reposo: aleatorio entre 0003..0009, evitando repetir el último
void reproducirIdleAleatorio() {
  if (dfIsPlaying()) { scheduleNextIdle(); return; }
  int span = (IDLE_MAX_INDEX - IDLE_MIN_INDEX + 1);
  if (span <= 0) { scheduleNextIdle(); return; }

  int nextIdx, intentos = 0;
  do {
    nextIdx = random(IDLE_MIN_INDEX, IDLE_MAX_INDEX + 1);
    intentos++;
  } while (nextIdx == ultimoIdle && span > 1 && intentos < 5);

  dfplayer.stop(); delay(20);
  dfplayer.playMp3Folder(nextIdx);
  ultimoIdle = nextIdx;
  scheduleNextIdle();
}
