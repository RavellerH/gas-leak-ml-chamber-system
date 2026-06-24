#include <Arduino.h>

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("SERIAL_HEALTH_CHECK_BOOT");
}

void loop()
{
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 1000)
  {
    lastPrint = millis();
    Serial.print("SERIAL_HEALTH_CHECK_OK ");
    Serial.println(millis());
  }
}
