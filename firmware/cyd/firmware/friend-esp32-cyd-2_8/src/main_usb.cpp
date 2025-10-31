#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }  // wait for USB CDC on S3
  Serial.println("{\"status\":\"ready\",\"app\":\"usb-link\"}");
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        line.trim();
        // simple commands to prove the pipe
        if (line.equalsIgnoreCase("start smile")) {
          Serial.println("{\"ack\":\"start_smile\"}");
          digitalWrite(LED_BUILTIN, HIGH);
        } else if (line.equalsIgnoreCase("stop")) {
          Serial.println("{\"ack\":\"stop\"}");
          digitalWrite(LED_BUILTIN, LOW);
        } else {
          Serial.print("{\"error\":\"unknown_cmd\",\"cmd\":\"");
          Serial.print(line);
          Serial.println("\"}");
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
}
