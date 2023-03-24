// Кодовий RFID замок на MFRC522. 
// використовуэмо WEMOS + TELEGRAM-bot
// Затримка на вхід, на вихід
// Сирена гудить одну хвилину
// стан охорони запамятовується в EEPROM

#define WIFI_SSID "Kyivstar2020"
#define WIFI_PASS "23153340"
#define BOT_TOKEN "6190794708:AAHcNfbd0BrbSAx1CX9e2BKQGBo79-RIRvQ"
#define CHAT_ID "5114790781"

#include <EEPROM.h>
#include <FastBot.h>
FastBot bot(BOT_TOKEN);

#include <SPI.h>
#include <MFRC522.h>
#include "pitches.h"

#define BUZZER          D0    // бузер
#define SIRENA          D1    // вихід на реле сирени
#define IRSENSOR        D2    // вхід датчик руху
#define GERKON          A0    // вхід Геркон
#define LEDALARM        D4    // Червоний - індикатор охорони

#define RST_PIN         D3    // Пин RST модуля rfid
#define SS_PIN          D8    // Пин SS модуля rfid

#define cIdleMode   0 //Охорона пасивна
#define cDelayOut   1 //Затримка на вихід
#define cArmedMode  2 //Охорона активна
#define cDelayIn    3 //Затримка на вхід
#define cSirenOn    4 //Лунає сирена
byte workMode = cIdleMode; // режим роботи


const unsigned int cSirenTimer = 1000 * 60; // Більше не ставити час роботи сирени
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
uint32_t  keys[] = {0x20546510, //#0-картка
                    0xF43BCF53, //#1
                    0xF4222333, //#2
                    0x244612D1, //#3
                    0xF43A4F73, //#4
                   };
byte nkey = 0; // номер розпізнаного ключа

void setup() {
  Serial.begin(115200);              // Инициализация Serial
  EEPROM.begin(10);
  workMode = readState(); // читаємо з EEPROM
  Serial.println();
  Serial.print("MODE=");
  Serial.println(workMode,HEX);
  Serial.println("Power On!");
  connectWiFi(); // підключаємося до мережі
  bot.attach(newMsg); // підключаємо функцію - обробник повідомлень
  // установка ID чата (белый список), необязательно. Можно несколько через запятую ("id1,id2,id3")
  bot.setChatID(CHAT_ID);
  bot.sendMessage("Живлення увімкнуто");

  // показать юзер меню (\t - горизонтальное разделение кнопок, \n - вертикальное
  //bot.showMenu("Menu1 \t Menu2 \t Menu3 \n Close");

  pinMode(LEDALARM, OUTPUT);  digitalWrite(LEDALARM, LOW);
  pinMode(BUZZER, OUTPUT);  digitalWrite(BUZZER, LOW);
  pinMode(SIRENA, OUTPUT);  digitalWrite(SIRENA, LOW);
  pinMode(GERKON, INPUT);
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

// Обробник повідомлень
void newMsg(FB_msg& msg) {
  String text = msg.text; // із структури повідомлення виділяємо сам текст
  String from_name = msg.username; // із структури повідомлення виділяємо хто надіслав
  if (from_name == "") from_name = "Анонім";

  // обробляємо натискання софт-кнопки в юзер меню в месенджері
  if (text == "Увімкнути сирену")
  {
    digitalWrite(SIRENA, HIGH); // вмикаємо сирену
    //previousMillis = millis();
    bot.sendMessage("Сирена ввімкнена", msg.chatID);
  }

  if (text == "Вимкнути сирену")
  {
    digitalWrite(SIRENA, LOW);
    bot.sendMessage("Сирена вимкнена", msg.chatID);
  }

  if (text == "Стан охорони")
  {
    //bot.sendMessage(String(workMode), msg.chatID);
    switch (workMode) {
      case cIdleMode:     bot.sendMessage("Пасивний режим", msg.chatID); break;
      case cDelayOut:     bot.sendMessage("Затримка на вихід", msg.chatID); break;
      case cArmedMode:    bot.sendMessage("Охорона увімкнена", msg.chatID); break;
      case cDelayIn:      bot.sendMessage("Затримка на вхід", msg.chatID); break;
      case cSirenOn:      bot.sendMessage("Лунає сирена", msg.chatID); break;
      default:  break;
    }
  }

  if (text == "Аптайм") { // рахуємо, скільки працює бот
    unsigned long myTime = millis();
    int mymin = myTime / 1000 / 60;
    int myhour = mymin / 60;
    int myday = myhour / 24;

    String msg1 = String(mymin % 60);
    String msg2 = String(myhour % 24);
    String msg3 = String(myday);
    // формуємо рядок з порахованими значеннями
    String msg0 = String("Я працюю " + msg3 + " діб " + msg2 + " годин " + msg1 + " хвилин");
    bot.sendMessage(msg0, msg.chatID); // відправляємо в месенджер
  }

  if (text == "/start") {// формуємо юзер меню з софт кнопками

    String welcome = "Вітаю, " + from_name + ".\n";
    bot.sendMessage(welcome, msg.chatID);
    // показати юзер меню (\t - горизонтальний поділ кнопок, \n - вертикальний
    bot.showMenu("Увімкнути сирену \t Вимкнути сирену \n Стан охорони \t Аптайм", msg.chatID);
  }

  /*if (text == "Сховати кнопки") {
    bot.sendMessage("Показати кнопки можна по команді /start", msg.chatID);
    bot.closeMenu(msg.chatID);
    }*/

}


void loop() {
  // Занимаемся чем угодно
  unsigned long myTime = millis();
  
  if (myTime/1000/60/60 > 1) ESP.restart();
    
  bot.tick();   // тикаем в луп
  //Serial.println(analogRead(GERKON));
  GerkonVal = (bool)(analogRead(GERKON) / 512); // імітуємо цифровий вхід
  GerkonVal = GerkonVal * (!IgnoreGerkon); //0=двері закриті
  //Serial.println(GerkonVal);

  IrSensorVal = digitalRead(IRSENSOR); //0=ніхто  не рухається

  switch (workMode) {
    case cIdleMode:     IdleMode(); break;
    case cDelayOut:     DelayOut(); break;
    case cArmedMode:    ArmedMode(); break;
    case cDelayIn:      DelayIn(); break;
    case cSirenOn:      SirenOn(); break;
    case 255:           workMode  = cIdleMode; // якщо EEPROM чиста
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
    if (readuid == keys[i]) {
      Serial.println(readuid, HEX);
      nkey = i;
      keyOk = HIGH;
    }
  }
}

void IdleMode() {
  //Охорона пасивна
  //Serial.println("IdleMode");
  digitalWrite(LEDALARM, LOW);
  digitalWrite(BUZZER, LOW);
  digitalWrite(SIRENA, LOW);
  IgnoreGerkon = 0; // не ігноруємо геркон при наступному ставанні на охорону

  if (keyOk && !IrSensorVal)  { // ЗОНА закрита & розпізнали ключ > DelayOut
    keyOk = 0; // скинули флаг
    workMode = cDelayOut;
    Serial.println("Delay Out");
    //bot.sendMessage("Delay Out");
    playBye();
    timerDelayOut = millis();
    return;
  }

  if (keyOk && IrSensorVal)  { // ЗОНА відкрита > довгий бузер і залиш. IdleMode
    keyOk = 0; // скинули флаг
    workMode = cIdleMode;
    Serial.println("Idle Mode");
    playCansel();
    return;
  }

}

void DelayOut() {
  //Затримка на вихід
  //Serial.println("Delay Out");

  // розпізнали ключ > IdleMode
  if (keyOk) {
    //digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    Serial.println("Idle Mode");
    keyOk = 0;
    playHello();
    return;
  }

  if (IrSensorVal) { // якщо ЗОНА відкрилась > довгий бузер і на IdleMode
    workMode = cIdleMode;
    Serial.println("Idle Mode");
    digitalWrite(LEDALARM, LOW);
    playCansel();
    return;
  }

  // ЗОНА закрита & Таймер все > ArmedMode
  if (millis() - timerDelayOut >= cDelayOutTimer) {
    //timerDelayOut = millis();               // Обновляем таймер
    digitalWrite(LEDALARM, HIGH);
    workMode = cArmedMode;
    saveState(workMode);
    Serial.println("Armed!");
    bot.sendMessage("Став на охорону ключом №" + String(nkey));

    // beep siren
    digitalWrite(SIRENA, HIGH);
    delay(500);
    digitalWrite(SIRENA, LOW);
    tone(BUZZER, 600, 100);

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
  //Serial.println("ArmedMode");
  digitalWrite(LEDALARM, HIGH);

  // ключ зчитаний?
  if (keyOk) {
    digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    saveState(workMode);
    Serial.println("Idle Mode");
    bot.sendMessage("Знято з охорони ключом №" + String(nkey));
    keyOk = 0;
    playHello();
    return;
  }

  // Датчик руху спрацював > SirenOn
  if (IrSensorVal) {
    digitalWrite(SIRENA, HIGH);
    workMode = cSirenOn;
    Serial.println("Siren On");
    bot.sendMessage("УВАГА!");
    bot.sendMessage("Спрацював датчик руху!");
    bot.sendMessage("Вмикаю сирену.");

    timerSirenOn = millis();
    return;
  }


  // Геркон спрацював > DelayIn
  if (GerkonVal) {
    workMode = cDelayIn;
    Serial.println("Delay In");
    bot.sendMessage("Відкрились двері");
    timerDelayIn = millis();
    return;
  }
}

void DelayIn() {
  //Затримка на вхід
  //Serial.println("Delay In");

  // ключ зчитаний?
  if (keyOk) { // розпізнали ключ > граємо мелодію > IdleMode
    //digitalWrite(SIRENA, LOW);
    //digitalWrite(LEDALARM, LOW);
    playHello();
    workMode = cIdleMode;
    saveState(workMode);
    Serial.println("Idle Mode");
    bot.sendMessage("Знято з охорони ключом №" + String(nkey));
    keyOk = 0;
    return;
  }


  // Таймер все > SirenOn
  if (millis() - timerDelayIn >= cDelayInTimer) {
    //timerDelayIn = millis();               // Обновляем таймер
    //digitalWrite(SIRENA, HIGH);
    //digitalWrite(LEDALARM, HIGH);
    workMode = cSirenOn;
    Serial.println("Siren On");
    bot.sendMessage("Вмикаю сирену");
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
  //Serial.println("SirenOn");


  // ключ зчитаний?
  if (keyOk) {
    digitalWrite(SIRENA, LOW);
    digitalWrite(LEDALARM, LOW);
    workMode = cIdleMode;
    saveState(workMode);
    Serial.println("Idle Mode");
    bot.sendMessage("Знято з охорони ключом №" + String(nkey));
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
    Serial.println("Armed!");
    bot.sendMessage("Сирену вимкнув. Став на охорону");
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
    tone(BUZZER, 100, 200);
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

// процедура підключення до мережі
void connectWiFi() {
  delay(2000);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() > 60000) {
      Serial.println("Fail conect");
      return;
    }
  }
  Serial.println("GOOD conect");
  //digitalWrite(LED_BUILTIN, LOW); // при вдалому підключенні горить світлодіод
}

void saveState(byte state){
  EEPROM.put(0, state);
  EEPROM.commit(); // для esp8266/esp32
}

byte readState(){
  byte state;
  EEPROM.get(0, state);
  return state;
}
