#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

// ========================= GRAVIX ZYRO BY AIRNEX =========================
// Receiver: ESP32-S3-32E N16R8; native USB HID to Windows 11.
// Packet layout/version must match Gravix_Zyro_Controller.ino.

constexpr uint8_t ESPNOW_CHANNEL=1;
constexpr uint16_t PACKET_MAGIC=0xC0DE;
constexpr uint8_t PACKET_VERSION=3;
constexpr uint8_t MODE_TOUCHPAD=0, MODE_JOYSTICK=1, MODE_AIR_MOUSE=2;
USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;

struct ControllerPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t mode;
  uint8_t buttons;
  uint8_t touchActive;
  uint8_t gesture;
  uint16_t touchX;
  uint16_t touchY;
  int16_t joyX;
  int16_t joyY;
  int16_t gyroX;
  int16_t gyroY;
  int16_t gyroZ;
  uint32_t sequence;
};
static_assert(sizeof(ControllerPacket)<=250,"Packet exceeds ESP-NOW limit");

portMUX_TYPE rxMux=portMUX_INITIALIZER_UNLOCKED;
ControllerPacket latestPacket={};
volatile bool packetPending=false;
volatile uint32_t lastRxMs=0, packetCount=0;
uint32_t lastSequence=0,lastDebugMs=0,previousInputMs=0;
bool havePacket=false,disconnected=true;
uint8_t mode=MODE_TOUCHPAD,previousButtons=0;

constexpr float JOY_SPEED=5.0f;
constexpr float AIR_GAIN=240.0f, AIR_DEADZONE=0.035f, AIR_FILTER=0.30f;
constexpr float TOUCH_GAIN=1.25f;
constexpr int TOUCH_JUMP_LIMIT=55;
constexpr int TAP_MOVE_LIMIT=8;
constexpr uint32_t TAP_MAX_MS=260;
constexpr uint32_t CLICK_DEBOUNCE_MS=28;

float airX=0,airY=0,fractionalX=0,fractionalY=0;
bool touchWasActive=false,touchMoved=false;
uint16_t prevTouchX=0,prevTouchY=0,touchStartX=0,touchStartY=0;
uint32_t touchStartMs=0;
uint32_t lastLeftEdgeMs=0,lastRightEdgeMs=0;
bool leftStable=false,rightStable=false;
uint8_t leftCandidate=0,rightCandidate=0;
uint32_t leftCandidateMs=0,rightCandidateMs=0;

void onDataReceive(const esp_now_recv_info_t *info,const uint8_t *data,int len) {
  (void)info;
  if (len!=(int)sizeof(ControllerPacket)) return;
  ControllerPacket p; memcpy(&p,data,sizeof(p));
  if (p.magic!=PACKET_MAGIC || p.version!=PACKET_VERSION || p.mode>MODE_AIR_MOUSE) return;
  portENTER_CRITICAL(&rxMux);
  latestPacket=p; packetPending=true; lastRxMs=millis(); packetCount++;
  portEXIT_CRITICAL(&rxMux);
}
bool startRadio() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  if (esp_wifi_set_channel(ESPNOW_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK) return false;
  if (esp_now_init()!=ESP_OK) return false;
  return esp_now_register_recv_cb(onDataReceive)==ESP_OK;
}
void releaseAllInputs() {
  Mouse.release(MOUSE_LEFT); Mouse.release(MOUSE_RIGHT); Mouse.release(MOUSE_MIDDLE);
  Keyboard.releaseAll();
  previousButtons=0; touchWasActive=false; touchMoved=false;
  airX=airY=fractionalX=fractionalY=0;
  leftStable=rightStable=false; leftCandidate=rightCandidate=0;
}
void moveMouseSmooth(float dx,float dy) {
  fractionalX+=dx; fractionalY+=dy;
  int mx=(int)fractionalX,my=(int)fractionalY;
  fractionalX-=mx; fractionalY-=my;
  mx=constrain(mx,-127,127); my=constrain(my,-127,127);
  if (mx||my) Mouse.move((int8_t)mx,(int8_t)my);
}
bool debounced(bool raw,uint8_t &candidate,uint32_t &candidateMs,bool &stable,uint32_t now) {
  uint8_t v=raw?1:0;
  if (v!=candidate) { candidate=v; candidateMs=now; }
  if (v!=stable && now-candidateMs>=CLICK_DEBOUNCE_MS) stable=v;
  return stable;
}
void processMouseButtons(uint8_t buttons,uint32_t now) {
  bool rawLeft=(buttons&(1<<0))!=0; // Only the dedicated physical left button.
  bool rawRight=(buttons&(1<<1))!=0;
  bool left=debounced(rawLeft,leftCandidate,leftCandidateMs,leftStable,now);
  bool right=debounced(rawRight,rightCandidate,rightCandidateMs,rightStable,now);
  static bool sentLeft=false,sentRight=false;
  if (left!=sentLeft) { if(left) Mouse.press(MOUSE_LEFT); else Mouse.release(MOUSE_LEFT); sentLeft=left; }
  if (right!=sentRight) { if(right) Mouse.press(MOUSE_RIGHT); else Mouse.release(MOUSE_RIGHT); sentRight=right; }
  bool backNow=(buttons&(1<<3))!=0;
  bool backBefore=(previousButtons&(1<<3))!=0;
  if(backNow&&!backBefore) {
    Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_LEFT_ARROW);
  } else if(!backNow&&backBefore) {
    Keyboard.release(KEY_LEFT_ARROW); Keyboard.release(KEY_LEFT_ALT);
  }
  previousButtons=buttons;
}
void processTouchpad(const ControllerPacket &p,uint32_t now) {
  bool active=p.touchActive!=0;
  if(active&&!touchWasActive) {
    touchStartX=prevTouchX=p.touchX; touchStartY=prevTouchY=p.touchY;
    touchStartMs=now; touchMoved=false;
  } else if(active&&touchWasActive) {
    int dx=(int)p.touchX-(int)prevTouchX;
    int dy=(int)p.touchY-(int)prevTouchY;
    if(abs(dx)<=TOUCH_JUMP_LIMIT&&abs(dy)<=TOUCH_JUMP_LIMIT) {
      if(abs((int)p.touchX-(int)touchStartX)>TAP_MOVE_LIMIT ||
         abs((int)p.touchY-(int)touchStartY)>TAP_MOVE_LIMIT) touchMoved=true;
      moveMouseSmooth(dx*TOUCH_GAIN,dy*TOUCH_GAIN);
    }
    prevTouchX=p.touchX; prevTouchY=p.touchY;
  } else if(!active&&touchWasActive) {
    if(!touchMoved && now-touchStartMs<TAP_MAX_MS) Mouse.click(MOUSE_LEFT);
  }
  touchWasActive=active;
}
void processJoystick(const ControllerPacket &p) {
  float x=p.joyX/1000.0f,y=p.joyY/1000.0f;
  // Squared curve gives fine control near center. No click is generated by
  // joystick motion; joystick switch is intentionally ignored in controller.
  x=copysignf(x*x,x); y=copysignf(y*y,y);
  moveMouseSmooth(x*JOY_SPEED,y*JOY_SPEED);
}
void processAirMouse(const ControllerPacket &p,float dt) {
  float gx=p.gyroX/1000.0f,gy=p.gyroY/1000.0f;
  if(fabsf(gx)<AIR_DEADZONE)gx=0;
  if(fabsf(gy)<AIR_DEADZONE)gy=0;
  airX+=AIR_FILTER*(gx-airX); airY+=AIR_FILTER*(gy-airY);
  moveMouseSmooth(airY*AIR_GAIN*dt,-airX*AIR_GAIN*dt);
}
void pressShortcut(uint8_t key,bool gui=false,bool alt=false) {
  if(gui) Keyboard.press(KEY_LEFT_GUI);
  if(alt) Keyboard.press(KEY_LEFT_ALT);
  Keyboard.press(key); delay(12); Keyboard.releaseAll();
}
void processGesture(uint8_t g) {
  // CST816D gesture register values vary by controller firmware/module.
  // Verify observed codes on your exact panel before relying on these mappings.
  switch(g) {
    case 0x01: pressShortcut(KEY_TAB,true,false); break;       // Task View
    case 0x02: Keyboard.press(KEY_LEFT_GUI); Keyboard.press('d'); delay(12); Keyboard.releaseAll(); break;
    case 0x03: pressShortcut(KEY_LEFT_ARROW,false,true); break;
    case 0x04: pressShortcut(KEY_RIGHT_ARROW,false,true); break;
    case 0x0B: Mouse.click(MOUSE_LEFT); break;
    case 0x0C: Mouse.click(MOUSE_RIGHT); break;
    default: break;
  }
}
void setup() {
  Serial.begin(115200); delay(500);
  Serial.println("\nGravix Zyro by AirNex receiver starting");
  Mouse.begin(); Keyboard.begin(); USB.begin();
  Serial.println("USB HID initialized");
  if(startRadio()) Serial.println("ESP-NOW ready; waiting for controller");
  else Serial.println("ERROR: ESP-NOW startup failed");
}
void loop() {
  uint32_t now=millis();
  ControllerPacket p={}; bool gotPacket=false;
  uint32_t rxTime,totalPackets;
  portENTER_CRITICAL(&rxMux);
  if(packetPending) { p=latestPacket; packetPending=false; gotPacket=true; }
  rxTime=lastRxMs; totalPackets=packetCount;
  portEXIT_CRITICAL(&rxMux);
  uint32_t age=(rxTime==0)?UINT32_MAX:now-rxTime;
  if(age>350) {
    if(!disconnected) { Serial.println("Controller timeout; releasing HID inputs"); releaseAllInputs(); disconnected=true; }
    delay(1); return;
  }
  if(!gotPacket||p.sequence==lastSequence) { delay(1); return; }
  lastSequence=p.sequence; disconnected=false;
  if(!havePacket) { havePacket=true; mode=p.mode; previousButtons=p.buttons; previousInputMs=now; delay(1); return; }
  if(p.mode!=mode) {
    releaseAllInputs(); mode=p.mode; previousButtons=p.buttons; previousInputMs=now;
    Serial.printf("Mode changed to %u\n",mode); delay(1); return;
  }
  float dt=(now-previousInputMs)/1000.0f; previousInputMs=now;
  dt=constrain(dt,0.001f,0.030f);
  if(mode==MODE_TOUCHPAD) {
    processMouseButtons(p.buttons,now);
    processTouchpad(p,now);
  } else if(mode==MODE_JOYSTICK) {
    processMouseButtons(p.buttons,now);
    processJoystick(p);
  } else if(mode==MODE_AIR_MOUSE) {
    processMouseButtons(p.buttons,now);
    processAirMouse(p,dt);
  }
  if(p.gesture!=0 && mode==MODE_TOUCHPAD) processGesture(p.gesture);
  if(now-lastDebugMs>=1000) {
    lastDebugMs=now;
    Serial.printf("RX=%lu age=%lu mode=%u seq=%lu gyro=%d,%d,%d\n",
      (unsigned long)totalPackets,(unsigned long)age,mode,
      (unsigned long)lastSequence,p.gyroX,p.gyroY,p.gyroZ);
  }
  delay(1);
}
