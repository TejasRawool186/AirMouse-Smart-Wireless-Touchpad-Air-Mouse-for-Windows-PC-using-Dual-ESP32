#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========================= GRAVIX ZYRO BY AIRNEX =========================
// Controller: ESP32-WROOM-32
// IMU is read directly over I2C because this module reports WHO_AM_I=0x70.
// Keep this packet structure synchronized with Gravix_Zyro_Receiver.ino.

constexpr int SDA_PIN = 21, SCL_PIN = 22;
constexpr int LCD_SCLK = 18, LCD_MISO = 19, LCD_MOSI = 23;
constexpr int LCD_CS = 5, LCD_DC = 27, LCD_RST = 33;
constexpr int LCD_BL = 17; // Change if your backlight is wired elsewhere.
constexpr int TOUCH_INT = 34, TOUCH_RST = 32;
constexpr int JOY_X = 36, JOY_Y = 39, JOY_SW = 35;
constexpr int BTN_LEFT = 13;
constexpr int BTN_RIGHT = 14;
constexpr int BTN_MODE = 25;
constexpr int BTN_BACK = 26;

constexpr uint8_t TOUCH_ADDR = 0x15;
constexpr uint8_t IMU_ADDR = 0x68;
constexpr uint8_t ESPNOW_CHANNEL = 1;

constexpr uint8_t MODE_TOUCHPAD = 0;
constexpr uint8_t MODE_JOYSTICK = 1;
constexpr uint8_t MODE_AIR_MOUSE = 2;

const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

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
static_assert(sizeof(ControllerPacket) <= 250, "Packet exceeds ESP-NOW limit");

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO, VSPI
);
Arduino_GFX *lcd = new Arduino_ST7789(lcdBus, LCD_RST, 0, true, 240, 320);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

bool lcdReady=false, oledReady=false, imuReady=false, radioReady=false;
uint8_t mode=MODE_TOUCHPAD, previousButtons=0, lastGesture=0;
uint16_t touchX=0, touchY=0;
bool touchActive=false;
int joyCenterX=2048, joyCenterY=2048;
float gyroBiasX=0, gyroBiasY=0, gyroBiasZ=0;
uint32_t sequenceNo=0, lastSendUs=0, lastDisplayMs=0, lastDebugMs=0;
uint8_t imuWhoAmI=0;

// ------------------------- IMU low-level access -------------------------
bool readImuReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IMU_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}
bool writeImuReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}
bool readImuBurst(uint8_t startReg, uint8_t *buf, size_t count) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom(IMU_ADDR, (uint8_t)count);
  if (got != count) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i=0; i<count; i++) buf[i] = Wire.read();
  return true;
}
int16_t be16(const uint8_t *p) {
  return (int16_t)((uint16_t(p[0]) << 8) | p[1]);
}
bool readGyroRad(float &gx, float &gy, float &gz) {
  uint8_t d[14];
  if (!readImuBurst(0x3B, d, sizeof(d))) return false;
  // Layout compatible with MPU6050/MPU6500-family registers:
  // accel X/Y/Z, temperature, gyro X/Y/Z.
  const int16_t rawX=be16(&d[8]), rawY=be16(&d[10]), rawZ=be16(&d[12]);
  // Gyro full-scale is configured to +/-500 degrees/sec (65.5 LSB/deg/s).
  constexpr float RAW_TO_RAD_S = 0.017453292519943295f / 65.5f;
  gx = rawX * RAW_TO_RAD_S - gyroBiasX;
  gy = rawY * RAW_TO_RAD_S - gyroBiasY;
  gz = rawZ * RAW_TO_RAD_S - gyroBiasZ;
  return true;
}
void calibrateGyroscope() {
  Serial.println("Keep Gravix Zyro still: calibrating gyro...");
  delay(700);
  double sx=0, sy=0, sz=0;
  int valid=0;
  for (int i=0; i<250; i++) {
    uint8_t d[14];
    if (readImuBurst(0x3B, d, sizeof(d))) {
      constexpr float k=0.017453292519943295f / 65.5f;
      sx += be16(&d[8])*k;
      sy += be16(&d[10])*k;
      sz += be16(&d[12])*k;
      valid++;
    }
    delay(4);
  }
  if (valid > 0) {
    gyroBiasX=sx/valid; gyroBiasY=sy/valid; gyroBiasZ=sz/valid;
  }
  Serial.printf("Gyro calibration samples=%d bias=%.4f,%.4f,%.4f rad/s\n",
                valid, gyroBiasX, gyroBiasY, gyroBiasZ);
}
void initImu() {
  if (!readImuReg(0x75, imuWhoAmI)) {
    Serial.println("IMU not responding at 0x68");
    return;
  }
  Serial.printf("IMU WHO_AM_I=0x%02X; using compatible raw register read\n", imuWhoAmI);
  // Reset and wake device; wait for startup.
  writeImuReg(0x6B, 0x80);
  delay(100);
  if (!writeImuReg(0x6B, 0x01)) {
    Serial.println("IMU wake/config failed"); return;
  }
  delay(20);
  // DLPF ~21 Hz; gyro +/-500 dps; accelerometer +/-4 g.
  writeImuReg(0x1A, 0x04);
  writeImuReg(0x1B, 0x08);
  writeImuReg(0x1C, 0x08);
  delay(20);
  uint8_t test[14];
  imuReady=readImuBurst(0x3B, test, sizeof(test));
  Serial.println(imuReady ? "IMU raw data ready" : "IMU raw read failed");
}
bool readTouch(uint8_t &gestureOut) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)6) != 6) {
    while (Wire.available()) Wire.read();
    return false;
  }
  uint8_t d[6]; for (int i=0;i<6;i++) d[i]=Wire.read();
  gestureOut=d[0];
  uint8_t fingers=d[1]&0x0F;
  uint16_t rawX=((d[2]&0x0F)<<8)|d[3];
  uint16_t rawY=((d[4]&0x0F)<<8)|d[5];
  touchActive=fingers>0;
  if (touchActive) {
    // Starting landscape transform; adjust if axes/direction are reversed.
    touchX=constrain(rawY,0,319);
    touchY=239-constrain(rawX,0,239);
  }
  return true;
}
uint8_t readButtons() {
  uint8_t b=0;
  if (digitalRead(BTN_LEFT)==LOW) b|=1<<0;
  if (digitalRead(BTN_RIGHT)==LOW) b|=1<<1;
  if (digitalRead(BTN_MODE)==LOW) b|=1<<2;
  if (digitalRead(BTN_BACK)==LOW) b|=1<<3;
  // Deliberately ignore JOY_SW: GPIO35 has no internal pull-up and a floating
  // switch can appear permanently pressed, causing accidental text selection.
  // Re-enable only after adding an external 10k pull-up to 3.3V.
  return b;
}
int16_t normalizeAxis(int raw, int center) {
  int delta=raw-center;
  constexpr int deadZone=180;
  if (abs(delta)<=deadZone) return 0;
  delta = delta>0 ? delta-deadZone : delta+deadZone;
  float n=(float)delta/(2048.0f-deadZone);
  n=constrain(n,-1.0f,1.0f);
  return (int16_t)lroundf(n*1000.0f);
}
void calibrateJoystick() {
  Serial.println("Leave joystick centered for calibration.");
  delay(500);
  long sx=0,sy=0;
  for (int i=0;i<150;i++) { sx+=analogRead(JOY_X); sy+=analogRead(JOY_Y); delay(4); }
  joyCenterX=sx/150; joyCenterY=sy/150;
  Serial.printf("Joystick center: %d,%d\n",joyCenterX,joyCenterY);
}
bool startRadio() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  if (esp_wifi_set_channel(ESPNOW_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK) return false;
  if (esp_now_init()!=ESP_OK) return false;
  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,BROADCAST_MAC,6);
  peer.channel=ESPNOW_CHANNEL; peer.encrypt=false;
  if (!esp_now_is_peer_exist(BROADCAST_MAC) &&
      esp_now_add_peer(&peer)!=ESP_OK) return false;
  Serial.print("Controller MAC: "); Serial.println(WiFi.macAddress());
  return true;
}
const char *modeName() {
  if (mode==MODE_TOUCHPAD) return "TOUCHPAD";
  if (mode==MODE_JOYSTICK) return "JOYSTICK";
  return "AIR MOUSE";
}
void updateDisplays(const ControllerPacket &p) {
  if (lcdReady) {
    lcd->fillScreen(BLACK); lcd->setTextColor(WHITE);
    lcd->setTextSize(2); lcd->setCursor(8,8); lcd->println("Gravix Zyro");
    lcd->setTextSize(1); lcd->setCursor(8,45); lcd->printf("By AirNex | %s",modeName());
    lcd->setCursor(8,68); lcd->printf("Touch: %s",p.touchActive?"ACTIVE":"READY");
    lcd->setCursor(8,87); lcd->printf("XY: %u,%u",p.touchX,p.touchY);
    lcd->setCursor(8,106); lcd->printf("Joy: %d,%d",p.joyX,p.joyY);
    lcd->setCursor(8,125); lcd->printf("IMU: %s (0x%02X)",imuReady?"OK":"FAIL",imuWhoAmI);
    lcd->setCursor(8,144); lcd->printf("Radio: %s",radioReady?"OK":"FAIL");
    lcd->setCursor(8,220); lcd->println("B1: Left click");
    lcd->setCursor(8,236); lcd->println("B2: Right click");
    lcd->setCursor(8,252); lcd->println("B3: Change mode");
    lcd->setCursor(8,268); lcd->println("B4: Back");
  }
  if (oledReady) {
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
    oled.setCursor(0,0); oled.println("Gravix Zyro");
    oled.print("Mode: "); oled.println(modeName());
    oled.print("Touch: "); oled.println(p.touchActive?"ON":"OFF");
    oled.print("Joy: "); oled.print(p.joyX); oled.print(","); oled.println(p.joyY);
    oled.print("IMU: "); oled.println(imuReady?"OK":"FAIL");
    oled.print("Radio: "); oled.println(radioReady?"OK":"FAIL");
    oled.display();
  }
}
void setup() {
  Serial.begin(115200); delay(500);
  Serial.println("\nGravix Zyro by AirNex controller starting");
  pinMode(BTN_LEFT,INPUT_PULLUP); pinMode(BTN_RIGHT,INPUT_PULLUP);
  pinMode(BTN_MODE,INPUT_PULLUP); pinMode(BTN_BACK,INPUT_PULLUP);
  pinMode(TOUCH_RST,OUTPUT); digitalWrite(TOUCH_RST,LOW); delay(5);
  digitalWrite(TOUCH_RST,HIGH); delay(50); pinMode(TOUCH_INT,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X,ADC_11db); analogSetPinAttenuation(JOY_Y,ADC_11db);
  Wire.begin(SDA_PIN,SCL_PIN,100000);
  initImu();
  oledReady=oled.begin(SSD1306_SWITCHCAPVCC,0x3C);
  pinMode(LCD_BL,OUTPUT); digitalWrite(LCD_BL,HIGH);
  lcdReady=lcd->begin(27000000);
  if (lcdReady) { lcd->setRotation(0); lcd->fillScreen(BLACK); }
  calibrateJoystick();
  if (imuReady) calibrateGyroscope();
  radioReady=startRadio();
  Serial.println("Controller ready");
}
void loop() {
  uint32_t nowUs=micros();
  if ((uint32_t)(nowUs-lastSendUs)<10000) { delay(1); return; }
  lastSendUs=nowUs;
  uint8_t buttons=readButtons();
  if ((buttons&(1<<2)) && !(previousButtons&(1<<2))) {
    mode=(mode+1)%3; Serial.printf("Mode: %s\n",modeName());
  }
  uint8_t gesture=0;
  if (!readTouch(gesture)) touchActive=false;
  uint8_t gestureEvent=0;
  if (gesture==0) lastGesture=0;
  else if (gesture!=lastGesture) { gestureEvent=gesture; lastGesture=gesture; }
  int16_t jx=normalizeAxis(analogRead(JOY_X),joyCenterX);
  int16_t jy=normalizeAxis(analogRead(JOY_Y),joyCenterY);
  float gx=0,gy=0,gz=0;
  if (imuReady) readGyroRad(gx,gy,gz);
  ControllerPacket p={};
  p.magic=0xC0DE; p.version=3; p.mode=mode; p.buttons=buttons;
  p.touchActive=touchActive?1:0; p.gesture=gestureEvent;
  p.touchX=touchX; p.touchY=touchY; p.joyX=jx; p.joyY=jy;
  p.gyroX=(int16_t)constrain(lroundf(gx*1000),-32768,32767);
  p.gyroY=(int16_t)constrain(lroundf(gy*1000),-32768,32767);
  p.gyroZ=(int16_t)constrain(lroundf(gz*1000),-32768,32767);
  p.sequence=++sequenceNo;
  if (radioReady) esp_now_send(BROADCAST_MAC,(const uint8_t*)&p,sizeof(p));
  uint32_t nowMs=millis();
  if (nowMs-lastDisplayMs>=500) { lastDisplayMs=nowMs; updateDisplays(p); }
  if (nowMs-lastDebugMs>=1000) {
    lastDebugMs=nowMs;
    Serial.printf("mode=%u touch=%u xy=%u,%u joy=%d,%d gyro=%d,%d,%d\n",
      p.mode,p.touchActive,p.touchX,p.touchY,p.joyX,p.joyY,p.gyroX,p.gyroY,p.gyroZ);
  }
  previousButtons=buttons;
}
