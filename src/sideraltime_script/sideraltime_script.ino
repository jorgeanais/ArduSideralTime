#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;

const float LONGITUD_SANTIAGO = -70.6693;
const int OFFSET_UTC = 4;
const int CORRECCION_SEG = 0;

unsigned long ultimaActualizacion = 0;

float calcularLST(int anio, int mes, int dia, int hora, int minuto, int segundo) {
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

  float LST = GST / 15.0 + LONGITUD_SANTIAGO / 15.0;
  LST = fmod(LST, 24.0);
  if (LST < 0) LST += 24.0;

  return LST;
}

void setup() {
  Serial.begin(9600);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  display.clearDisplay();
  display.display();
}

void loop() {
  unsigned long ahora = millis();
  if (ahora - ultimaActualizacion < 1000) return;
  ultimaActualizacion = ahora;

  DateTime local = rtc.now();
  DateTime utc = DateTime(local.unixtime() + OFFSET_UTC * 3600UL + CORRECCION_SEG);

  float lst = calcularLST(
    utc.year(), utc.month(), utc.day(),
    utc.hour(), utc.minute(), utc.second()
  );

  int lst_h = (int)lst;
  int lst_m = (int)((lst - lst_h) * 60);
  int lst_s = (int)(((lst - lst_h) * 60 - lst_m) * 60);

  display.clearDisplay();

  // Título
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 0);
  display.print("T. Sideral Local");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Ubicación
  display.setCursor(34, 13);
  display.print("SCL -70.67W");

  // Fecha UTC
  display.setCursor(0, 23);
  display.print("UTC: ");
  display.print(utc.year(), DEC);
  display.print('/');
  if (utc.month() < 10) display.print('0');
  display.print(utc.month(), DEC);
  display.print('/');
  if (utc.day() < 10) display.print('0');
  display.print(utc.day(), DEC);

  // Hora UTC
  display.setCursor(0, 33);
  display.print("UTC: ");
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
