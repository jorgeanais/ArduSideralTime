#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <TinyGPS++.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// --- GPS ---
// El Uno solo tiene UN puerto UART de hardware (pines 0/1), el mismo que
// usa el USB para el Monitor Serie y para subir sketches. Lo usamos
// tambien para el GPS en vez de SoftwareSerial: SoftwareSerial reserva su
// propio buffer + logica adicional en RAM, y en un Uno (solo 2KB de RAM)
// eso deja muy poco margen para el buffer de pantalla del SSD1306, que se
// reserva dinamicamente (malloc) al llamar display.begin(). Si ese malloc
// no encuentra espacio, falla con "SSD1306 allocation failed" -- que es
// justo el sintoma que se ve al agregar GPS con SoftwareSerial.
//
// Cableado: GPS TX -> Arduino pin RX0 (pin 0). GPS RX no se usa (no le
// enviamos comandos), puede ir sin conectar.
//
// IMPORTANTE: desconecta el cable GPS TX -> RX0 mientras subes el sketch
// (el programador usa ese mismo pin), y reconectalo despues de subir.
#define GPS_BAUD 9600

// Reintentar sincronizar el RTC con el GPS cada 10 minutos si hay fix
const unsigned long RESYNC_INTERVAL_MS = 10UL * 60UL * 1000UL;
// Tiempo maximo esperando el primer fix al encender (ms)
const unsigned long TIMEOUT_FIX_INICIAL_MS = 45000UL;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
TinyGPSPlus gps;

// Longitud usada si el GPS no tiene fix (oeste = negativo)
const float LONGITUD_SANTIAGO = -70.6693;
const int   CORRECCION_SEG    = 0; // ajuste fino manual si aun lo necesitas

unsigned long ultimaActualizacion = 0;
unsigned long ultimoResync = 0;
bool gpsSincronizadoAlgunaVez = false;
bool gpsFixReciente = false; // fix visto en los ultimos segundos (para UI)

float calcularLST(int anio, int mes, int dia, int hora, int minuto, int segundo, float longitud) {
  if (mes <= 2) { anio--; mes += 12; }
  int A = anio / 100;
  int B = 2 - A + A / 4;
  long JD0 = (long)(365.25 * (anio + 4716))
           + (long)(30.6001 * (mes + 1))
           + dia + B - 1524;

  long D_entero = JD0 - 2451545L;
  float D_frac  = -0.5 + (hora + minuto / 60.0 + segundo / 3600.0) / 24.0;

  float GST = 280.46061837
            + fmod(360.98564736629 * D_entero, 360.0)
            + 360.98564736629 * D_frac;

  GST = fmod(GST, 360.0);
  if (GST < 0) GST += 360.0;

  float LST = GST / 15.0 + longitud / 15.0;
  LST = fmod(LST, 24.0);
  if (LST < 0) LST += 24.0;

  return LST;
}

// Lee todo lo disponible del GPS (por el UART de hardware) y, si hay
// datos de hora/fecha frescos y validos, ajusta el RTC (que pasa a
// guardar UTC directamente). Devuelve true si el RTC fue actualizado.
bool actualizarDesdeGPS() {
  while (Serial.available() > 0) {
    gps.encode(Serial.read());
  }

  gpsFixReciente = gps.location.isValid() && gps.location.age() < 3000;

  bool horaValida  = gps.time.isValid() && gps.time.age() < 1000;
  bool fechaValida = gps.date.isValid() && gps.date.age() < 1000;

  if (horaValida && fechaValida && gps.date.year() >= 2020) {
    DateTime utcGPS(gps.date.year(), gps.date.month(), gps.date.day(),
                     gps.time.hour(), gps.time.minute(), gps.time.second());
    rtc.adjust(utcGPS);
    gpsSincronizadoAlgunaVez = true;
    return true;
  }
  return false;
}

void setup() {
  // Un solo Serial.begin: se usa tanto para el Monitor Serie (debug)
  // como para leer al GPS, ya que comparten el mismo UART de hardware.
  Serial.begin(GPS_BAUD);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1);
  }

  if (rtc.lostPower()) {
    // Solo como ultimo respaldo si el RTC nunca tuvo bateria/hora.
    // Sera corregido en cuanto el GPS obtenga el primer fix.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Buscar el primer fix GPS antes de arrancar el loop principal,
  // para partir con la hora UTC correcta desde el encendido.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Buscando GPS..."));
  display.display();

  unsigned long inicio = millis();
  while (millis() - inicio < TIMEOUT_FIX_INICIAL_MS) {
    if (actualizarDesdeGPS()) break;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print(F("Buscando GPS..."));
    display.setCursor(0, 12);
    display.print(F("Sats: "));
    display.print(gps.satellites.isValid() ? gps.satellites.value() : 0);
    display.setCursor(0, 24);
    display.print(F("Chars: "));
    display.print(gps.charsProcessed());
    display.display();
  }

  if (!gpsSincronizadoAlgunaVez) {
    Serial.println(F("Sin fix GPS inicial, usando hora del RTC."));
    if (gps.charsProcessed() < 10) {
      Serial.println(F("Aviso: no llegan datos NMEA. Revisa el cableado TX/RX y el baudrate."));
    }
  }

  ultimoResync = millis();
  display.clearDisplay();
  display.display();
}

void loop() {
  // Alimentar el parser GPS en cada vuelta del loop (no solo 1 vez/seg).
  while (Serial.available() > 0) {
    gps.encode(Serial.read());
  }

  unsigned long ahora = millis();

  if (ahora - ultimoResync >= RESYNC_INTERVAL_MS) {
    ultimoResync = ahora;
    actualizarDesdeGPS();
  } else {
    // igual actualiza el flag de fix reciente para la UI
    gpsFixReciente = gps.location.isValid() && gps.location.age() < 3000;
  }

  if (ahora - ultimaActualizacion < 1000) return;
  ultimaActualizacion = ahora;

  // El RTC ya guarda UTC (fue sincronizado por GPS), solo se aplica
  // la correccion fina manual en segundos si el usuario la definio.
  DateTime utc = DateTime(rtc.now().unixtime() + CORRECCION_SEG);

  float longitudActual = LONGITUD_SANTIAGO;
  if (gpsFixReciente) {
    longitudActual = gps.location.lng();
  }

  float lst = calcularLST(
    utc.year(), utc.month(), utc.day(),
    utc.hour(), utc.minute(), utc.second(),
    longitudActual
  );

  int lst_h = (int)lst;
  int lst_m = (int)((lst - lst_h) * 60);
  int lst_s = (int)(((lst - lst_h) * 60 - lst_m) * 60);

  display.clearDisplay();

  // Titulo
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 0);
  display.print(F("T. Sideral Local"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Estado GPS + longitud usada
  display.setCursor(0, 13);
  if (gpsFixReciente) {
    display.print(F("GPS "));
    display.print(gps.satellites.value());
    display.print(F("sat "));
  } else {
    display.print(F("RTC  "));
  }
  display.print(longitudActual, 2);
  display.print((char)247); // grado

  // Fecha UTC
  display.setCursor(0, 25);
  display.print(F("UTC: "));
  display.print(utc.year(), DEC);
  display.print('/');
  if (utc.month() < 10) display.print('0');
  display.print(utc.month(), DEC);
  display.print('/');
  if (utc.day() < 10) display.print('0');
  display.print(utc.day(), DEC);

  // Hora UTC
  display.setCursor(0, 35);
  display.print(F("UTC: "));
  if (utc.hour() < 10) display.print('0');
  display.print(utc.hour());
  display.print(':');
  if (utc.minute() < 10) display.print('0');
  display.print(utc.minute());
  display.print(':');
  if (utc.second() < 10) display.print('0');
  display.print(utc.second());

  // LST en grande
  display.setTextSize(2);
  display.setCursor(14, 46);
  if (lst_h < 10) display.print('0');
  display.print(lst_h);
  display.print(':');
  if (lst_m < 10) display.print('0');
  display.print(lst_m);
  display.print(':');
  if (lst_s < 10) display.print('0');
  display.print(lst_s);

  display.display();
}
