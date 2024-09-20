#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"

SSD1306AsciiAvrI2c OLED;

#define PW_PIN 3
#define S0_PIN 8
#define S1_PIN 9
#define S2_PIN 10
#define S3_PIN 11
#define SI_PIN A0

int PWM  = 0;
float R1 = 10000.0; // 10k
float R2 = 2200.0;  // 2.2k
bool isRunning = false;

String padLeft(int value, char ins, int len) {
  String temp = String(value);
  while (temp.length() < len) temp = String(ins) + temp;
  return temp;
}

String padRight(String temp, char ins, int len) {
  while (temp.length() < len) temp = temp + String(ins);
  return temp;
}

String padRight(int value, char ins, int len) {
  String temp = String(value);
  while (temp.length() < len) temp = temp + String(ins);
  return temp;
}

String padRight(float value, char ins, int len, int decimalPlaces) {
  String temp = String(value, decimalPlaces);
  while (temp.length() < len) temp = temp + String(ins);
  return temp;
}

struct ServiceHandler {
  virtual ~ServiceHandler() = default;
  virtual void onEvent(){};
  virtual void onTrigger(){};
  virtual void onDisplay(){};
  virtual void onMeasure(){};
  virtual void onTimer(){};
};

class ActionController {
  private:
    ServiceHandler* fnc = nullptr;
  public:
    ActionController& invoke(void (ServiceHandler::*function)()) {
      if(fnc)(fnc->*function)();
      return *this;
    }
    ActionController& registerHandler(ServiceHandler* fnc) {
      this->fnc = fnc;
      return *this;
    }
    ActionController& registerHandler(ServiceHandler& fnc) {
      return registerHandler(&fnc);
    }
};

struct Multiplexer : ActionController {
  private:
    const byte SMOOTHING_FACTOR = 10;
    int muxValues[16] = {};
    
    int count = 0;
    long voltageSum = 0, currentSum = 0, tempSum = 0;
    float voltage = 0.0, current = 0.0, temp = 0.0;

    byte seconds = 0, minutes = 0, hours = 0;
    unsigned long lastTime = 0, timerInterval = 0, tempInterval = 0;
  public:
    Multiplexer(){
      byte pins[] = {S0_PIN, S1_PIN, S2_PIN, S3_PIN,PW_PIN};
      for (byte pin : pins) {
        pinMode(pin, OUTPUT);
      }
      pinMode(SI_PIN, INPUT);
      analogWrite(PW_PIN,PWM);
    }
    void setChannel(byte ch) {
      digitalWrite(S0_PIN, ch & 0x01);
      digitalWrite(S1_PIN, (ch >> 1) & 0x01);
      digitalWrite(S2_PIN, (ch >> 2) & 0x01);
      digitalWrite(S3_PIN, (ch >> 3) & 0x01);
    }
    float averageReadings(byte pin, int samples) {
      long sum = 0;
      for (int i = 0; i < samples; i++) sum += analogRead(pin);
      return sum / samples;
    }
    void onListener() {
      for (int ch = 0; ch < 16; ch++) {
        setChannel(ch);
        muxValues[ch] = averageReadings(SI_PIN, SMOOTHING_FACTOR);
      }

      voltageSum += selectChannel(5);
      currentSum += selectChannel(6);
      tempSum    += selectChannel(7);
      
      if(++count >= 20) updateReadings();
      
      invoke(&ServiceHandler::onEvent);
      onTimerTick();
    }
    void updateReadings() {
      float voltageAt = (voltageSum / count) * (5.0 / 1024.0) / (R2 / (R1 + R2));
      voltage = roundToThreshold(voltageAt, 2);

      float currentAt = voltage - map(currentSum / count, 0, 1020, 1020, 0) * (voltage / 1024.0);
      current = roundToThreshold(currentAt, 2);

      float tempAt = tempSum / count * (5.0 / 1024.0);
      temp    = roundToThreshold(tempAt, 2);
      
      count   = voltageSum = currentSum = tempSum = 0;
      invoke(&ServiceHandler::onMeasure);
    }
    int selectChannel(byte ch){
      return muxValues[ch-1];
    }
    float roundToThreshold(float value, int decimals) {
      float multiplier = pow(10, decimals);
      return round(value * multiplier) / multiplier;
    }
    float getVoltage(){
      return voltage;
    }
    float getCurrent(){
      return current;
    }
    float getTemp(){
      return temp;
    }
    void setTimer(bool start, long interval) {
      isRunning     = start;
      timerInterval = interval;
      tempInterval  = interval;
      seconds       = 0;
    }
    void onTimerTick(){
      if (!isRunning) return;
      
      unsigned long curr = millis();
      if (curr - lastTime >= 1000) {
        updateClock();
        lastTime = curr;
        if (isRunning) invoke(&ServiceHandler::onTimer);
      }
    }
    void updateClock() {
      if (timerInterval > 0) {
        isRunning = tempInterval > 0;
        seconds   = (--tempInterval) % 60;
        hours     = (tempInterval / 3600);
        minutes   = (tempInterval / 60) % 60;
      } else {
        minutes += (++seconds) / 60;
        hours   += minutes / 60;
        seconds %= 60;
        minutes %= 60;
        hours   %= 99;
      }
    }
    String getClock(){
      return padLeft(hours,'0',2) +":"+ padLeft(minutes,'0',2) +":"+ padLeft(seconds,'0',2);
    }
};

enum ButtonEnum{
  LEFT, UP, DOWN, RIGHT, NONE
};
    
struct ButtonMonitor : ServiceHandler, Multiplexer{
  private:
    static const byte THRESHOLD_MIN      = 1;
    static const byte THRESHOLD_MAX      = 5;
    static const byte DEBOUNCE_PERIOD_MS = 80;
    unsigned long lastPressTime = 0;
    unsigned int holdCount      = 0;
    ButtonEnum state            = NONE;
  public:
    int currentIndex = 0;
    bool isStage     = true;
    ButtonMonitor(){
      registerHandler(this);
    }
    ButtonEnum readButtonState(){
      if(selectChannel(1) >= 900) return RIGHT;
      if(selectChannel(2) >= 900) return DOWN;
      if(selectChannel(3) >= 900) return UP;
      if(selectChannel(4) >= 900) return LEFT;
      return NONE;
    }
    void onEvent() override {
      unsigned long currentTime = millis();
      if(currentTime - lastPressTime > DEBOUNCE_PERIOD_MS){
        state = this->readButtonState();

        holdCount = (state != NONE) ? holdCount + 1 : 0;
        if(holdCount == THRESHOLD_MIN || holdCount >= THRESHOLD_MAX){
          invoke(&ServiceHandler::onTrigger);
        }
        
        lastPressTime = currentTime;
      }
    }
    ButtonEnum getState(){
      return state;
    }
    String getStateName(){
      switch(state){
        case LEFT  : return "LEFT";
        case UP    : return "UP";
        case DOWN  : return "DOWN";
        case RIGHT : return "RIGHT";
      }
      return "NONE";
    }
    int adjustIndex(ButtonEnum button, int currentIndex, int menuSize) {
        if (button == UP) {
            return (currentIndex + 1) % menuSize;
        } else if (button == DOWN) {
            return (currentIndex - 1 + menuSize) % menuSize;
        }
        return currentIndex;
    }
};

struct MainMenu : ServiceHandler,ButtonMonitor{
  public:
    void onTrigger() override {
      ButtonEnum btn = this->getState();
      if(btn == LEFT){
        isStage = false;
        setTimer(false,0);
        return;
      }
      if(btn == RIGHT){
        isStage = true;
        setTimer(true,0);
        return;
      }

      if(btn == UP){
        PWM += 5;
      }else if(btn == DOWN){
        PWM -= 5;
      }

      PWM = (PWM >= 255) ? 255 : (PWM <= 0) ? 0 : PWM;
      analogWrite(PW_PIN,PWM);
      
      onDisplay();
    }
    void onMeasure() override {
      onDisplay();
    }
    void onTimer() override{
      Serial.println(getClock());
    }
    void onDisplay() override{
      // Line 0
      OLED.setCursor(0,0);
      OLED.print("BTN");
      OLED.setCursor(25,0);
      OLED.print("=");
      OLED.setCursor(35,0);
      OLED.print(padRight(this->getStateName(),' ',5));
      // Line 1
      OLED.setCursor(0,2);
      OLED.print("TMP");
      OLED.setCursor(25,2);
      OLED.print("=");
      OLED.setCursor(35,2);
      OLED.print(padRight(getTemp(),' ',5,2));
      // Line 2
      OLED.setCursor(0,4);
      OLED.print("PWM");
      OLED.setCursor(25,4);
      OLED.print("=");
      OLED.setCursor(35,4);
      OLED.print(padRight(PWM,' ',5));
      // Line 3
      OLED.setCursor(0,6);
      OLED.print("VAL");
      OLED.setCursor(25,6);
      OLED.print("=");
      OLED.setCursor(35,6);
      OLED.print(padRight(getVoltage(),' ',5,2));
      OLED.setCursor(77,6);
      OLED.print("/");
      OLED.setCursor(85,6);
      OLED.print(padRight(getCurrent(),' ',5,2));
    }
};
MainMenu* mainMenu;

void setup(){
  Serial.begin(9600);
  mainMenu = new MainMenu();
  OLED.begin(&Adafruit128x64, 0x3C, 0);
  OLED.setFont(ZevvPeep8x16);
  
  OLED.set1X();
  OLED.clear();
  
  mainMenu->onDisplay();
}

void loop(){
  mainMenu->onListener();
}
