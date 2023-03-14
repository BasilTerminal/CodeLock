// Кодовий RFID замок на MFRC522. Arduino Nano
// Затримка на вхід, на вихід
// Сирена гудить одну хвилину
#include <SPI.h>
#include <MFRC522.h> // https://github.com/miguelbalboa/rfid
#include "pitches.h"

#define BUZZER          A1    // бузер
#define SIRENA          A5    // вихід на реле сирени
#define IRSENSOR        2     // вхід датчик руху
#define GERKON          5     // вхід Геркон
#define LEDALARM        6     // Червоний - індикатор охорони

#define RST_PIN         9     // Пин rfid модуля RST
#define SS_PIN          10    // Пин rfid модуля SS


#define cIdleMode   0 //Охорона пасивна
#define cDelayOut   1 //Затримка на вихід
#define cArmedMode  2 //Охорона активна
#define cDelayIn    3 //Затримка на вхід
#define cSirenOn    4 //Лунає сирена
//byte workMode = cIdleMode; // режим роботи
byte workMode = cIdleMode; // режим роботи


const unsigned int cSirenTimer = 1000*60; // Більше не ставити час роботи сирени
#define cDelayInTimer 1000*10 // затримка на вхід
#define cDelayOutTimer 1000*10 // затримка на вихід

uint32_t timerSirenOn, timerDelayIn, timerDelayOut;

MFRC522 rfid(SS_PIN, RST_PIN);   // Объект rfid модуля
MFRC522::MIFARE_Key key;         // Объект ключа
MFRC522::StatusCode status;      // Объект статуса

uint32_t readuid = 0;           // зчитаний ключ
bool keyOk = 0;                 //флаг, що ключ вдало зчитаний
bool GerkonVal = 0;             // зчитаний стан геркона 0 - закриті двері
bool IrSensorVal = 0;           // зчитаний стан датчика руху 0 - ніхто не ходить
bool IgnoreGerkon = 0;           // ігнорувати геркон після першого вмикання сирени?

#define AMOUNTKEYS  5 // число ключей
uint32_t cards[] = {0x20546510, //#0-картка
                    0xF43BCF53, //#1
                    0xF4222333, //#2
                    0x244612D1, //#3
                    0xF43A4F73, //#4
                   };

void setup() {
  Serial.begin(115200);              // Инициализация Serial

  pinMode(LEDALARM, OUTPUT);  digitalWrite(LEDALARM, LOW);
  pinMode(BUZZER, OUTPUT);  digitalWrite(BUZZER, LOW);
  pinMode(SIRENA, OUTPUT);  digitalWrite(SIRENA, LOW);
  pinMode(GERKON, INPUT_PULLUP);
  pinMode(IRSENSOR, INPUT_PULLUP);

  // beep buzzer
  playPowerOn();

  // beep siren
  digitalWrite(SIRENA, HIGH);
  delay(500);
  digitalWrite(SIRENA, LOW);

  // blink
  digitalWrite(LEDALARM, HIGH);
  delay(500);
  digitalWrite(LEDALARM, LOW);

  SPI.begin();                     // Инициализация SPI
  rfid.PCD_Init();                 // Инициализация модуля
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);  // Установка усиления антенны
  rfid.PCD_AntennaOff();           // Перезагружаем антенну
  rfid.PCD_AntennaOn();            // Включаем антенну

  for (byte i = 0; i < 6; i++) {   // Наполняем ключ
    key.keyByte[i] = 0xFF;         // Ключ по умолчанию 0xFFFFFFFFFFFF
  }

  timerSirenOn = millis();
  timerDelayIn = millis();
  timerDelayOut = millis();

}

void loop() {
  // Занимаемся чем угодно

  GerkonVal = digitalRead(GERKON)*(!IgnoreGerkon); //0=двері закриті
  IrSensorVal = digitalRead(IRSENSOR); //0=ніхто  не рухається

  switch (workMode) {
    case cIdleMode:     IdleMode(); break;
    case cDelayOut:     DelayOut(); break;
    case cArmedMode:    ArmedMode(); break;
    case cDelayIn:      DelayIn(); break;
    case cSirenOn:      SirenOn(); break;
    default:  break;
  }

  static uint32_t rebootTimer = millis(); // Важный костыль против зависания модуля!
  if (millis() - rebootTimer >= 1000) {   // Таймер с периодом 1000 мс
    rebootTimer = millis();               // Обновляем таймер
    digitalWrite(RST_PIN, HIGH);          // Сбрасываем модуль
    delayMicroseconds(2);                 // Ждем 2 мкс
    digitalWrite(RST_PIN, LOW);           // Отпускаем сброс
    rfid.PCD_Init();                      // Инициализируем заного
  }

  if (!rfid.PICC_IsNewCardPresent()) return;  // Если новая метка не поднесена - вернуться в начало loop
  if (!rfid.PICC_ReadCardSerial()) return;    // Если метка не читается - вернуться в начало loop

  byte byte3 = rfid.uid.uidByte[3];
  byte byte2 = rfid.uid.uidByte[2];
  byte byte1 = rfid.uid.uidByte[1];
  byte byte0 = rfid.uid.uidByte[0];
  readuid = ((uint32_t)byte3 << 24) | ((uint32_t)byte2 << 16) | ((uint32_t)byte1 << 8) | (uint32_t)byte0;
  for (uint8_t i = 0; i < AMOUNTKEYS; i++) {       // какой номер
    if (readuid == cards[i]) {
      keyOk = HIGH;
    }
  }
}

void IdleMode() {
  //Охорона пасивна
  Serial.println("IdleMode");
  digitalWrite(LEDALARM, LOW);
  digitalWrite(BUZZER, LOW);
  digitalWrite(SIRENA, LOW);
  IgnoreGerkon = 0; // не ігноруємо геркон при наступному ставанні на охорону

  if (keyOk && !IrSensorVal)  { // ЗОНА закрита & розпізнали ключ > DelayOut
    keyOk = 0; // скинули флаг
    workMode = cDelayOut;
    playBye();
    timerDelayOut = millis();
    return;
  }

  if (keyOk && IrSensorVal)  { // ЗОНА відкрита > довгий бузер і залиш. IdleMode
    keyOk = 0; // скинули флаг
    workMode = cIdleMode;
    playCansel();
    return;
  }

}

void DelayOut() {
  //Затримка на вихід
  Serial.println("Delay Out");

  // розпізнали ключ > IdleMode
  if (keyOk) {
    //digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    keyOk = 0;
    playHello();
    return;
  }

  if (IrSensorVal) { // якщо ЗОНА відкрилась > довгий бузер і на IdleMode
    workMode = cIdleMode;
    digitalWrite(LEDALARM, LOW);
    playCansel();
    return;
  }

  // ЗОНА закрита & Таймер все > ArmedMode
  if (millis() - timerDelayOut >= cDelayOutTimer) {
    //timerDelayOut = millis();               // Обновляем таймер
    digitalWrite(LEDALARM, HIGH);
    workMode = cArmedMode;
    
    // beep siren
    digitalWrite(SIRENA, HIGH);
    delay(500);
    digitalWrite(SIRENA, LOW);
    tone(BUZZER,100,200);
    
    return;
  }

  // поки триває таймаут, блимаємо червоним та пищимо бузером
  static uint32_t timerLedAlarm = millis(); //
  static bool LEDflag = false; // флаг
  if (millis() - timerLedAlarm >= 1000) { // 1 сек
    timerLedAlarm = millis();               // Обновляем таймер
    LEDflag = !LEDflag; // инвертировать флаг
    digitalWrite(LEDALARM, LEDflag);
    tone(BUZZER, 1000, 100);
    return;
  }
}

void ArmedMode() {
  //Охорона активна
  Serial.println("ArmedMode");
  digitalWrite(LEDALARM, HIGH);

  // ключ зчитаний?
  if (keyOk) {
    digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    keyOk = 0;
    playHello();
    return;
  }

  // Датчик руху спрацював > SirenOn
  if (IrSensorVal) {
    digitalWrite(SIRENA, HIGH);
    workMode = cSirenOn;
    timerSirenOn = millis();
    return;
  }


  // Геркон спрацював > DelayIn
  if (GerkonVal) {
    workMode = cDelayIn;
    timerDelayIn = millis();
    return;
  }
}

void DelayIn() {
  //Затримка на вхід
  Serial.println("Delay In");

  // ключ зчитаний?
  if (keyOk) { // розпізнали ключ > граємо мелодію > IdleMode
    //digitalWrite(SIRENA, LOW);
    //digitalWrite(LEDALARM, LOW);
    playHello();
    workMode = cIdleMode;
    keyOk = 0;
    return;
  }


  // Таймер все > SirenOn
  if (millis() - timerDelayIn >= cDelayInTimer) {
    //timerDelayIn = millis();               // Обновляем таймер
    //digitalWrite(SIRENA, HIGH);
    //digitalWrite(LEDALARM, HIGH);
    workMode = cSirenOn;
    timerSirenOn = millis();
    IgnoreGerkon = 1; // після виходу ігноруємо геркон
    return;
  }


  // блимаємо червоним та пищимо бузером
  static uint32_t timerLedAlarm = millis(); //
  static bool LEDflag = false; // флаг
  if (millis() - timerLedAlarm >= 500) { //
    timerLedAlarm = millis();               // Обновляем таймер
    LEDflag = !LEDflag; // инвертировать флаг
    digitalWrite(LEDALARM, LEDflag);
    tone(BUZZER, 2000, 100);
  }
}

void SirenOn() {
  static bool LEDflag = false; // флаг
  //Лунає сирена
  Serial.println("SirenOn");
  

  // ключ зчитаний?
  if (keyOk) {
    digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    keyOk = 0;
    LEDflag = false;
    playHello();
    return;
  }

  // таймер все > ArmedMode
  if (millis() - timerSirenOn >= cSirenTimer) {
    //timerSirenOn = millis();               // Обновляем таймер
    digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, HIGH);
    workMode = cArmedMode;
    LEDflag = false;
    return;
  }

  // блимаємо червоним та гудимо сиреною
  static uint32_t timerLedAlarm = millis(); //
  if (millis() - timerLedAlarm >= 1000 * 1) { //
    timerLedAlarm = millis();               // Обновляем таймер
    LEDflag = !LEDflag; // инвертировать флаг
    digitalWrite(LEDALARM, LEDflag);
    digitalWrite(SIRENA, LEDflag);
    tone(BUZZER,100,200);
  }
}

void playPowerOn() {
  tone(BUZZER, 500);
  delay(100);
  tone(BUZZER, 700);
  delay(100);
  tone(BUZZER, 500);
  delay(100);
  tone(BUZZER, 900);
  delay(100);
  tone(BUZZER, 500);
  delay(100);
  tone(BUZZER, 1000);
  delay(100);
  noTone(BUZZER);
}

void playCansel() {
  // грає неприємний звук коли ЗОНА відкрита
  tone(BUZZER, 200);
  delay(2000);
  noTone(BUZZER);
}

void playBye() {
  // грає прощальну мелодію
  int melody[] = {
    NOTE_A4,   NOTE_A4,   NOTE_A4,   NOTE_G4,   NOTE_A4,
    NOTE_A4,   NOTE_A4,   NOTE_F4,
    NOTE_C4,   NOTE_F4,   NOTE_C4,   NOTE_F4,
  };

  // note durations: 4 = quarter note, 8 = eighth note, etc.:
  int noteDurations[] = {
    8, 8, 8, 2, 8,
    8, 8, 2,
    8, 4, 8, 2
  };
  for (int thisNote = 0; thisNote < 12; thisNote++) {
    int noteDuration = 1000 / noteDurations[thisNote];
    tone(BUZZER, melody[thisNote], noteDuration);
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    noTone(BUZZER);
  }
}

void playHello() {
  // грає вітальну мелодію
  int melody[] = {
    NOTE_C4,   NOTE_D4,   NOTE_A4,   NOTE_D5,
    NOTE_F5,   NOTE_A4,   NOTE_D5,   NOTE_F5
  };
  int noteDurations[] = {
    8, 8, 8, 8, 8, 8, 8, 8
  };
  for (int thisNote = 0; thisNote < 8; thisNote++) {
    int noteDuration = 1000 / noteDurations[thisNote];
    tone(BUZZER, melody[thisNote], noteDuration);
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    noTone(BUZZER);
  }
}
