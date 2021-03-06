#include <SoftwareSerial.h>
#include <ss_oled.h>
#include "rtc.h"
#include "RTClib.h"

SSOLED ssoled;
#define SDA_PIN -1
#define SCL_PIN -1
#define RESET_PIN -1
#define OLED_ADDR -1
#define FLIP180 0
#define INVERT 0
#define USE_HW_I2C 1

#define TxD 11
#define RxD 12
const int BT_TIMEOUT = 60000 * 3;

const int SWITCH_DELAY = 500;
enum SwitchRole { NEXT_PAGE, DOWN, SELECT, SECONDARY, SET, INCREASE, MAIN, START_STOP };
struct SwitchState {
  const int pin0 = 2;
  const int pin1 = 3;
  volatile int flag0 = LOW;
  volatile int flag1 = LOW;
  int role0 = SECONDARY;
  int role1 = NEXT_PAGE;
  unsigned long lastPress = 0;
};
struct LastState {
  unsigned int minuteTime = 100;
  unsigned int dayDate = 100;
};
struct PageState {
  int current = 0;
  unsigned int max = 1;
};
struct SecondaryState {
  bool toggle = false;
  bool isOn = false;
  bool downFlag = false;
  bool selectFlag = false;
  unsigned int currentOption = 0;
  unsigned int lastOption = 0;
  unsigned int currentMaxOption = 0;
  char options[5][20];
  int optionsRole[5];
};
enum TimerIncrement {THOUR, TMINUTE, TSECOND};
struct TimerState {
  volatile int hour = 0;
  volatile int minute = 0;
  volatile int second = 0;
  int currentIncrement = THOUR;
  bool isOn = false;
  unsigned long targetDiffStamp = 0;
  unsigned long startStamp = 0;
};

SecondaryState secondary;
PageState page;
LastState last;
volatile SwitchState switchS;
volatile TimerState timer;
RTC_DS3231 rtc;
SoftwareSerial btSerial(RxD, TxD); 

void switch0Changed() {
  switchS.flag0 = digitalRead(switchS.pin0);
}
void switch1Changed() {
  switchS.flag1 = digitalRead(switchS.pin1);
}

void formatDate(char* dateBuf) {
  snprintf(dateBuf, 13, "%04d. %02d. %02d.", getRtcTime(rtc, YEAR), getRtcTime(rtc, MONTH), getRtcTime(rtc, DAY));
}
void drawDate() {
  char dateBuf[13];
  formatDate(dateBuf);
  oledWriteString(&ssoled, 0, 0, 0, dateBuf, FONT_SMALL, 0, 1);
}

void formatTime(char* clockBuf) {
  snprintf(clockBuf, 6, "%02d:%02d", getRtcTime(rtc, HOUR), getRtcTime(rtc, MINUTE));
}
void drawTime() {
  char clockBuf[5];
  formatTime(clockBuf);
  oledWriteString(&ssoled, 0, 25, 3, clockBuf, FONT_STRETCHED, 0, 1);  
}
void refreshTime() {
  unsigned int currentMinute = getRtcTime(rtc, MINUTE);
  if (currentMinute != last.minuteTime) {
    last.minuteTime = currentMinute; 
    drawTime();
  }
}

void formatValue(char* valueBuf, char* text, char* value, int size) {
  snprintf(valueBuf, size, "%s: %02d", text, value);
}
void drawTimerSetting(bool title = false) {
  if (title) {
    oledWriteString(&ssoled, 0, 0, 0, "Timer", FONT_NORMAL, 1, 1);
  }
  char hourBuf[8]; formatValue(hourBuf, "Hour", timer.hour, 9);
  char minuteBuf[11]; formatValue(minuteBuf, "Min.", timer.minute, 11);
  char secondBuf[11]; formatValue(secondBuf, "Sec.", timer.second, 11);
  oledWriteString(&ssoled, 0, 0, 2, hourBuf, FONT_STRETCHED, THOUR == timer.currentIncrement, 1);
  oledWriteString(&ssoled, 0, 0, 4, minuteBuf, FONT_STRETCHED, TMINUTE == timer.currentIncrement, 1);
  oledWriteString(&ssoled, 0, 0, 6, secondBuf, FONT_STRETCHED, TSECOND == timer.currentIncrement, 1);
}
void startTimer() {
  timer.startStamp = rtc.now().unixtime();
  timer.targetDiffStamp = ((timer.hour * 60 * 60) + (timer.minute * 60) + timer.second) + timer.startStamp;
  oledFill(&ssoled, 0, 1);
  timer.isOn = true;
}

void refreshDate() {
  unsigned int currentDay = getRtcTime(rtc, DAY);
  if (currentDay != last.dayDate) {
    last.dayDate = currentDay; 
    drawDate();
  }
}

void refreshMain() {
  refreshDate();
  refreshTime();
}
void refreshPage() {
  oledFill(&ssoled, 0, 1);
  switchS.role0 = SECONDARY;
  switchS.role1 = NEXT_PAGE;
  switch (page.current) {
    case -1: {
      switchS.role0 = SET;
      switchS.role1 = INCREASE;
      drawTimerSetting(true);
      break;
    }
    case 0: {
      drawDate();
      drawTime();
      break;
    }
    case 1: {
      drawDate();
      break;  
    }
  }
}

void incrementPage() {
    if (page.current >= page.max) {
      page.current = 0;
    } else {
      page.current++;
    }
}
void handleSwitches() {
  if (millis() - switchS.lastPress >= SWITCH_DELAY) {
    if (switchS.flag0 == HIGH) {
      switchS.lastPress = millis();
      switch(switchS.role0) {
        case SECONDARY: {
          secondary.toggle = true;
          break;  
        }
        case DOWN: {
          secondary.downFlag = true;
          break;
        }
        case SET: {
          if (timer.currentIncrement == TSECOND) {
            startTimer();
          } else {
            timer.currentIncrement++;
            drawTimerSetting();
          }
          break;  
        }
      }
    }
    if (switchS.flag1 == HIGH) {
      switchS.lastPress = millis();   
      switch(switchS.role1) {
        case NEXT_PAGE: {
          incrementPage();
          refreshPage();
          break;
        }
        case SELECT: {
          secondary.selectFlag = true;
          break;
        }
        case INCREASE: {
          int *ptr;
          switch(timer.currentIncrement) {
            case THOUR: {
              ptr = &timer.hour;
              break;
            }
            case TMINUTE: {
              ptr = &timer.minute;
              break;
            }
            case TSECOND: {
              ptr = &timer.second;
              break;
            }
          }
          if (*ptr < 59) {
            (*ptr)++;
          } else {
            *ptr = 0;
          }
          drawTimerSetting();
          break;  
        }
      }
    }
  }
}

void drawTimeSync() {
  oledWriteString(&ssoled, 0, 0, 0, "Syncing over BT...", FONT_SMALL, 0, 1);
}

void btSafeReadLine(char* outStr, int len) {
  btSerial.readString().toCharArray(outStr, len);
}
void btSafePrintLn(String str) {
  noInterrupts();
  btSerial.println(str);
  interrupts();
}
void waitForBt() {
  unsigned long waitingStart = millis();
  while(btSerial.available() == 0 && millis() - waitingStart < BT_TIMEOUT) { }
}
void syncTimeBT() {
  btSafePrintLn("TIME");
  waitForBt();
  char input[20];
  btSafeReadLine(input, 20);
  DateTime date;
  tokenizeDate(&date, input);
  setRtcTime(rtc, date);
}

void formatTimer(char* timerBuf) {
  snprintf(timerBuf, 11, "%02d:%02d:%02d", timer.hour, timer.minute, timer.second);
}
void drawTimer() {
  char timerBuf[10];
  formatTimer(timerBuf);
  oledWriteString(&ssoled, 0, 0, 3, timerBuf, FONT_STRETCHED, 0, 1);
}
void handleTimer() {
  unsigned long currentStamp = rtc.now().unixtime();
  unsigned long currentDiffStamp = currentStamp - timer.startStamp;
  if (timer.targetDiffStamp == currentDiffStamp) {
    drawDate();
    //reset timer n shit
  } else {
    timer.hour = floor(currentDiffStamp / 60 / 60);
    timer.minute = floor(currentDiffStamp / 60);
    while (timer.minute < 60) {
      timer.minute -= 60;
    }
    timer.second = currentDiffStamp;
    while (timer.second < 60) {
      timer.second -= 60;
    }
    drawTimer();
  }
}

enum secondaryOptionRole{ BACK, SYNC_TIME_BT, TIMER, STOPWATCH };
void drawSecondaryOption(int index = -1) {
  int offset = 2;
  if (index == -1) {
    for (int i = 0; i < secondary.currentMaxOption; i++ ) {
      oledWriteString(&ssoled, 0, 0, offset + i, secondary.options[i], FONT_NORMAL, secondary.currentOption == i, 1);
    } 
  } else {
    oledWriteString(&ssoled, 0, 0, offset + secondary.lastOption, secondary.options[secondary.lastOption], FONT_NORMAL, 0, 1);
    oledWriteString(&ssoled, 0, 0, offset + secondary.currentOption, secondary.options[index], FONT_NORMAL, 1, 1);
  }
}
void drawSecondary() {
  switchS.role0 = DOWN;
  switchS.role1 = SELECT;
  oledFill(&ssoled, 0, 1);
  oledWriteString(&ssoled, 0, 0, 0, "Secondary", FONT_NORMAL, 1, 1);
  for (int i = 0; i < secondary.currentMaxOption; i++ ) {
    strcpy(secondary.options[i], "");
  } 
  secondary.currentOption = 0;
  secondary.currentMaxOption = 1;
  strcpy(secondary.options[0], "Back");
  secondary.optionsRole[0] = BACK;
  switch (page.current) {
    case 0: {
      strcpy(secondary.options[1], "Sync time (BT)");
      secondary.optionsRole[1] = SYNC_TIME_BT;
      
      strcpy(secondary.options[2], "Timer");
      secondary.optionsRole[2] = TIMER;
      
      strcpy(secondary.options[3], "Stopwatch");
      secondary.optionsRole[3] = STOPWATCH;
      
      secondary.currentMaxOption += 3;
      drawSecondaryOption();
    }    
  }
}
void handleSecondary() {
  if (secondary.downFlag) {
  secondary.downFlag = false;
  secondary.lastOption = secondary.currentOption;
  if (secondary.currentOption+1 < secondary.currentMaxOption) {
    secondary.currentOption++;
  } else {
    secondary.currentOption = 0;
  }
  drawSecondaryOption(secondary.currentOption);
  }
  if (secondary.selectFlag) {
    secondary.selectFlag = false;
    switch(secondary.optionsRole[secondary.currentOption]) {
      case BACK: {
        secondary.toggle = true;
        break;
      }
      case SYNC_TIME_BT: {
        secondary.toggle = true;
        syncTime();
        break;
      }
      case TIMER: {
        secondary.toggle = true;
        page.current = -1;
        break;
      }
      case STOPWATCH: {
        secondary.toggle = true;
        page.current = -2;
        break;
      }
    }
  }
}
void switchSecondary() {
  if (secondary.toggle) {
    secondary.toggle = false;
    if (secondary.isOn) {
      refreshPage();
      secondary.isOn = false;
    } else {
      drawSecondary();
      secondary.isOn = true;
    }
  }  
}

void syncTime() {
  oledFill(&ssoled, 0, 1);
  drawTimeSync();
  syncTimeBT();
}
void setupSwitches() {
  pinMode(switchS.pin0, INPUT);
  pinMode(switchS.pin1, INPUT);
  attachInterrupt(digitalPinToInterrupt(switchS.pin0), switch0Changed, CHANGE);
  attachInterrupt(digitalPinToInterrupt(switchS.pin1), switch1Changed, CHANGE);
}
bool initScreen() {
  if (oledInit(&ssoled, OLED_128x64, OLED_ADDR, FLIP180, INVERT, USE_HW_I2C, SDA_PIN, SCL_PIN, RESET_PIN, 400000L) == OLED_NOT_FOUND) {
    return false;
  }
  oledSetContrast(&ssoled, 150);
  oledFill(&ssoled, 0, 1);
  return true;
}

void setup() {
  btSerial.begin(9600); 
  btSafePrintLn("START");
  if (!initScreen()) {
    btSafePrintLn("ERR");
    for (;;) {}
  }
  
  rtc.begin();
  if (rtc.lostPower()) {
      syncTime();
  }
  refreshPage();
  setupSwitches();
}

void loop() {
  if (page.current == 0 && !secondary.isOn) {
    refreshMain();
  }
  handleSwitches();
  switchSecondary();
  if (secondary.isOn) {
    handleSecondary();
  }
  if (timer.isOn) {
    handleTimer();
  }
}
