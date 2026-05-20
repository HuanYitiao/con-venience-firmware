#include <Arduino.h>
#include "driver/uart.h"

#define ACOM_TX   22
#define ACOM_RX   23
#define ACOM_OE   21  // 74HC126 OE引脚
#define ACOM_BAUD 9600

void acomTxEnable() {
  digitalWrite(ACOM_OE, HIGH);  // OE=HIGH，TX信号通过74HC126
}

void acomTxDisable() {
  digitalWrite(ACOM_OE, LOW);   // OE=LOW，TX高阻，让开信号线
}

void acomSend(const char* msg) {
  // 先听，有数据就不发
  if (Serial2.available()) return;
  
  acomTxEnable();
  delayMicroseconds(200);
  Serial2.print(msg);
  Serial2.flush();
  acomTxDisable();
  
  // 清空自己echo
  delay(10);
  while (Serial2.available()) Serial2.read();
}

void setup() {
  Serial.begin(115200);
  pinMode(ACOM_OE, OUTPUT);
  acomTxDisable();  // 默认不发送
  Serial2.begin(ACOM_BAUD, SERIAL_8N1, ACOM_RX, ACOM_TX);
  Serial.println("ready");
}

void loop() {
  if (Serial2.available()) {
    String received = "";
    delay(20);
    while (Serial2.available()) {
      received += (char)Serial2.read();
    }
    Serial.println("received: " + received);
    delay(100);
    acomSend("PONG\n");
    Serial.println("sent: PONG");
  }

  static unsigned long last = 0;
  if (millis() - last > 3000) {
    last = millis();
    acomSend("PING\n");
    Serial.println("sent: PING");
  }
}