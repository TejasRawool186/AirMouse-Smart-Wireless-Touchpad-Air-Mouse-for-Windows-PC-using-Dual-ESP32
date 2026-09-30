/*
  Handheld Controller - ESP32-WROOM-32

  Hardware:
    - Waveshare 2inch Capacitive Touch LCD (ST7789T3 + CST816D)
    - MPU6050
    - SSD1306 I2C OLED 128x64
    - 2-axis analog joystick + SW
    - 5 push buttons
    - ESP-NOW transmitter

  Arduino-ESP32 target: 3.x

  IMPORTANT WIRING USED BY THIS SKETCH
  Waveshare 15-pin LCD:
    VCC    -> NC
    3V3    -> ESP32 3V3
    GND    -> ESP32 GND
    MISO   -> GPIO19
    MOSI   -> GPIO23
    SCLK   -> GPIO18
    SD_CS  -> NC
    LCD_CS -> GPIO5
    LCD_DC -> GPIO27
    LCD_RST-> GPIO33
    LCD_BL -> GPIO17
    TP_SDA -> GPIO21
    TP_SCL -> GPIO22
    TP_INT -> GPIO34 (optional in software; input only)
    TP_RST -> GPIO32

  MPU6050:
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO21
    SCL -> GPIO22
    AD0 -> GND (address 0x68)
    XDA/XCL/INT -> NC

  OLED 128x64 I2C:
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO21
    SCL -> GPIO22

  Joystick:
    VRX -> GPIO36
    VRY -> GPIO39
    SW  -> GPIO35
    VCC -> 3V3
    GND -> GND

  Buttons (4-pin tactile switches: use one pin from each electrical pair):
    B1 -> GPIO13 + GND
    B2 -> GPIO14 + GND
    B3 -> GPIO25 + GND
    B4 -> GPIO26 + GND
    B5 -> GPIO4  + GND

  Button logic: INPUT_PULLUP, pressed = LOW.

  This sketch reads the CST816D directly over I2C, so no separate CST library is required.
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

#include <Arduino_GFX_Library.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// -------------------------- Pin definitions --------------------------
static constexpr int I2C_SDA = 21;
static constexpr int I2C_SCL = 22;

static constexpr int LCD_MISO = 19;
static constexpr int LCD_MOSI = 23;
static constexpr int LCD_SCLK = 18;
static constexpr int LCD_CS   = 5;
static constexpr int LCD_DC   = 27;
static constexpr int LCD_RST  = 33;
static constexpr int LCD_BL   = 17;

static constexpr int TP_INT = 34;
static constexpr int TP_RST = 32;

static constexpr int JOY_X_PIN = 36;
static constexpr int JOY_Y_PIN = 39;
static constexpr int JOY_SW_PIN = 35;

static constexpr int B1_PIN = 13;
static constexpr int B2_PIN = 14;
static constexpr int B3_PIN = 25;
static constexpr int B4_PIN = 26;
static constexpr int B5_PIN = 4;

// -------------------------- I2C devices ------------------------------
static constexpr uint8_t CST816D_ADDR = 0x15;
static constexpr uint8_t MPU6050_ADDR = 0x68;

// -------------------------- ESP-NOW ----------------------------------
static constexpr uint8_t ESPNOW_CHANNEL = 1;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// One stable, self-contained packet used by both ESP32s.
struct ControllerPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t mode;       // 0 = mouse, 1 = keyboard-assist
  uint8_t buttons;    // bit0 B1, bit1 B2, bit2 B3, bit3 B4, bit4 B5, bit5 joystick SW
  uint8_t gesture;    // CST816D gesture code, 0 if none
  uint32_t seq;
  int16_t joy_x;      // -1000 .. +1000
  int16_t joy_y;      // -1000 .. +1000
  uint16_t touch_x;   // 0..239
  uint16_t touch_y;   // 0..319
  int16_t gyro_x;     // milli-rad/s
  int16_t gyro_y;
  int16_t gyro_z;
  int16_t accel_x;    // milli-m/s^2
  int16_t accel_y;
  int16_t accel_z;
  uint32_t uptime_ms;
};

static_assert(sizeof(ControllerPacket) <= 250, "ESP-NOW packet is too large");

// -------------------------- Display ---------------------------------
Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
  LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO, VSPI);

Arduino_GFX *gfx = new Arduino_ST7789(
  lcdBus,
  LCD_RST,
  0,       // rotation
  true,    // IPS
  240,     // width
  320      // height
);

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledOk = false;
uint8_t oledAddress = 0;

Adafruit_MPU6050 mpu;
bool imuOk = false;

// -------------------------- State -----------------------------------
uint32_t sequenceNumber = 0;
uint8_t currentMode = 0;
uint8_t lastButtons = 0;
uint8_t lastGestureSent = 0;

int joyCenterX = 2048;
int joyCenterY = 2048;

float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

uint16_t lastTouchX = 0;
uint16_t lastTouchY = 0;
uint8_t lastGesture = 0;
bool touchActive = false;

uint32_t lastSendMs = 0;
uint32_t lastUiMs = 0;
uint32_t lastPacketPrintMs = 0;
uint32_t sendCount = 0;

// -------------------------- Helpers ---------------------------------
static void requireI2CDevice(uint8_t address, const char *name) {
  Wire.beginTransmission(address);
  uint8_t err = Wire.endTransmission();
  Serial.print(name);
  Serial.print(" @0x");
  if (address < 16) Serial.print('0');
  Serial.print(address, HEX);
  Serial.print(": ");
  Serial.println(err == 0 ? "OK" : "NOT FOUND");
}

static int16_t clampI16(long value) {
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<int16_t>(value);
}

static int16_t axisToSigned1000(int raw, int center) {
  // Keep a small dead-zone around center to stop cursor drift.
  const int dead = 120;
  int delta = raw - center;
  if (abs(delta) <= dead) return 0;

  if (delta > 0) delta -= dead;
  else delta += dead;

  const int span = 2048 - dead;
  long out = (static_cast<long>(delta) * 1000L) / span;
  if (out > 1000) out = 1000;
  if (out < -1000) out = -1000;
  return static_cast<int16_t>(out);
}

static uint8_t readButtons() {
  uint8_t b = 0;
  if (digitalRead(B1_PIN) == LOW) b |= (1u << 0);
  if (digitalRead(B2_PIN) == LOW) b |= (1u << 1);
  if (digitalRead(B3_PIN) == LOW) b |= (1u << 2);
  if (digitalRead(B4_PIN) == LOW) b |= (1u << 3);
  if (digitalRead(B5_PIN) == LOW) b |= (1u << 4);
  if (digitalRead(JOY_SW_PIN) == LOW) b |= (1u << 5);
  return b;
}

// -------------------------- CST816D ---------------------------------
// CST816D register map: gesture 0x01, finger count 0x02,
// X high/low 0x03/0x04, Y high/low 0x05/0x06.
static bool cst816dReadTouch(uint16_t &x, uint16_t &y, uint8_t &gesture, bool &active) {
  Wire.beginTransmission(CST816D_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;

  const uint8_t want = 6;
  uint8_t got = Wire.requestFrom(CST816D_ADDR, want);
  if (got != want) return false;

  uint8_t buf[6];
  for (uint8_t i = 0; i < want; ++i) buf[i] = Wire.read();

  gesture = buf[0];
  uint8_t fingerCount = buf[1];
  x = static_cast<uint16_t>(((buf[2] & 0x0F) << 8) | buf[3]);
  y = static_cast<uint16_t>(((buf[4] & 0x0F) << 8) | buf[5]);
  active = (fingerCount > 0);

  if (x >= 240) x = 239;
  if (y >= 320) y = 319;
  return true;
}

static bool cst816dReadChipId(uint8_t &id) {
  Wire.beginTransmission(CST816D_ADDR);
  Wire.write(0xA7);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(CST816D_ADDR, (uint8_t)1) != 1) return false;
  id = Wire.read();
  return true;
}

static void cst816dReset() {
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(5);
  digitalWrite(TP_RST, HIGH);
  delay(50);
}

// -------------------------- ESP-NOW ---------------------------------
static bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  esp_err_t chErr = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (chErr != ESP_OK) {
    Serial.printf("esp_wifi_set_channel failed: %d\n", chErr);
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return false;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
      Serial.println("ESP-NOW add broadcast peer failed");
      return false;
    }
  }

  Serial.print("Controller MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("ESP-NOW transmitter ready");
  return true;
}

// -------------------------- Calibration -----------------------------
static void calibrateJoystick() {
  Serial.println("Leave joystick centered: calibrating...");
  const int samples = 600;
  long sumX = 0;
  long sumY = 0;

  for (int i = 0; i < samples; ++i) {
    sumX += analogRead(JOY_X_PIN);
    sumY += analogRead(JOY_Y_PIN);
    delay(2);
  }

  joyCenterX = static_cast<int>(sumX / samples);
  joyCenterY = static_cast<int>(sumY / samples);

  Serial.printf("Joystick center X=%d Y=%d\n", joyCenterX, joyCenterY);
}

static void calibrateGyro() {
  if (!imuOk) return;

  Serial.println("Keep controller still: calibrating gyro...");
  const int samples = 400;
  double sx = 0, sy = 0, sz = 0;

  sensors_event_t accel, gyro, temp;
  for (int i = 0; i < samples; ++i) {
    if (mpu.getEvent(&accel, &gyro, &temp)) {
      sx += gyro.gyro.x;
      sy += gyro.gyro.y;
      sz += gyro.gyro.z;
    }
    delay(5);
  }

  gyroBiasX = static_cast<float>(sx / samples);
  gyroBiasY = static_cast<float>(sy / samples);
  gyroBiasZ = static_cast<float>(sz / samples);

  Serial.printf("Gyro bias: %.5f %.5f %.5f rad/s\n", gyroBiasX, gyroBiasY, gyroBiasZ);
}

// -------------------------- UI --------------------------------------
static void drawLCD(const ControllerPacket &p, bool espnowOk, bool touchOk) {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(8, 8);
  gfx->print("HAND CONTROLLER");

  gfx->setTextSize(1);
  gfx->setCursor(8, 40);
  gfx->print("Mode: ");
  gfx->print(p.mode == 0 ? "MOUSE" : "KEYBOARD");

  gfx->setCursor(8, 58);
  gfx->print("Joy X: "); gfx->print(p.joy_x);
  gfx->setCursor(8, 72);
  gfx->print("Joy Y: "); gfx->print(p.joy_y);

  gfx->setCursor(8, 90);
  gfx->print("Touch: ");
  if (touchOk) {
    gfx->print(p.touch_x);
    gfx->print(",");
    gfx->print(p.touch_y);
  } else {
    gfx->print("--");
  }

  gfx->setCursor(8, 108);
  gfx->print("Gesture: "); gfx->print(p.gesture);

  gfx->setCursor(8, 126);
  gfx->print("B1-5: "); gfx->print(p.buttons & 0x1F, BIN);

  gfx->setCursor(8, 144);
  gfx->print("IMU: "); gfx->print(imuOk ? "OK" : "FAIL");

  gfx->setCursor(8, 162);
  gfx->print("ESP-NOW: "); gfx->print(espnowOk ? "OK" : "FAIL");

  gfx->setCursor(8, 180);
  gfx->print("Packets: "); gfx->print(sendCount);

  gfx->setCursor(8, 212);
  gfx->setTextSize(1);
  gfx->print("B1 Left  B2 Right");
  gfx->setCursor(8, 228);
  gfx->print("B3 Mode  B4 Back");
  gfx->setCursor(8, 244);
  gfx->print("B5 Forward  JS=Middle");
}

static void drawOLED(const ControllerPacket &p, bool espnowOk) {
  if (!oledOk) return;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("ESP32 Controller");
  oled.setCursor(0, 10);
  oled.print("Mode: "); oled.print(p.mode == 0 ? "MOUSE" : "KEYS");
  oled.setCursor(0, 20);
  oled.print("J:"); oled.print(p.joy_x); oled.print(","); oled.print(p.joy_y);
  oled.setCursor(0, 30);
  oled.print("T:"); oled.print(p.touch_x); oled.print(","); oled.print(p.touch_y);
  oled.setCursor(0, 40);
  oled.print("G:"); oled.print(p.gesture); oled.print(" B:"); oled.print(p.buttons, HEX);
  oled.setCursor(0, 50);
  oled.print(espnowOk ? "ESP-NOW OK" : "ESP-NOW FAIL");
  oled.display();
}

// -------------------------- Main ------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32 HAND CONTROLLER - START");
  Serial.println("========================================");

  // Buttons
  pinMode(B1_PIN, INPUT_PULLUP);
  pinMode(B2_PIN, INPUT_PULLUP);
  pinMode(B3_PIN, INPUT_PULLUP);
  pinMode(B4_PIN, INPUT_PULLUP);
  pinMode(B5_PIN, INPUT_PULLUP);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  // Touch control lines
  pinMode(TP_INT, INPUT);
  cst816dReset();

  // ADC setup
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X_PIN, ADC_11db);
  analogSetPinAttenuation(JOY_Y_PIN, ADC_11db);

  // Shared I2C bus. CST816D datasheet recommends max 400 kHz.
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(20);

  requireI2CDevice(CST816D_ADDR, "CST816D");
  requireI2CDevice(MPU6050_ADDR, "MPU6050");

  uint8_t chipId = 0;
  if (cst816dReadChipId(chipId)) {
    Serial.printf("CST816D ChipID: 0x%02X\n", chipId);
  } else {
    Serial.println("CST816D ChipID read failed");
  }

  // MPU6050
  if (mpu.begin(MPU6050_ADDR, &Wire)) {
    imuOk = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 ready");
  } else {
    Serial.println("MPU6050 NOT FOUND - continuing without IMU");
  }

  // OLED auto-detect 0x3C then 0x3D.
  if (oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledOk = true;
    oledAddress = 0x3C;
  } else if (oled.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    oledOk = true;
    oledAddress = 0x3D;
  }

  if (oledOk) {
    Serial.printf("SSD1306 ready @0x%02X\n", oledAddress);
    oled.clearDisplay();
    oled.display();
  } else {
    Serial.println("SSD1306 NOT FOUND - continuing without OLED");
  }

  // LCD
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  if (!gfx->begin(27000000)) {
    Serial.println("LCD init returned false");
  } else {
    gfx->setRotation(0);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(12, 120);
    gfx->print("STARTING...");
  }

  calibrateJoystick();
  calibrateGyro();

  bool espnowOk = initEspNow();

  ControllerPacket bootPacket{};
  bootPacket.magic = 0xC0DE;
  bootPacket.version = 1;
  bootPacket.mode = currentMode;
  bootPacket.buttons = 0;
  bootPacket.gesture = 0;
  bootPacket.seq = sequenceNumber;
  bootPacket.joy_x = 0;
  bootPacket.joy_y = 0;
  bootPacket.touch_x = 0;
  bootPacket.touch_y = 0;
  bootPacket.uptime_ms = millis();

  if (espnowOk) {
    esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&bootPacket), sizeof(bootPacket));
  }

  Serial.println("Ready.");
}

void loop() {
  const uint32_t now = millis();

  // Read buttons once per loop.
  uint8_t buttons = readButtons();

  // B3 toggles local mode on press edge.
  const bool b3Now = (buttons & (1u << 2)) != 0;
  const bool b3Before = (lastButtons & (1u << 2)) != 0;
  if (b3Now && !b3Before) {
    currentMode = (currentMode == 0) ? 1 : 0;
    Serial.print("Mode -> ");
    Serial.println(currentMode == 0 ? "MOUSE" : "KEYBOARD");
  }
  lastButtons = buttons;

  // Read joystick.
  const int rawJoyX = analogRead(JOY_X_PIN);
  const int rawJoyY = analogRead(JOY_Y_PIN);
  const int16_t joyX = axisToSigned1000(rawJoyX, joyCenterX);
  const int16_t joyY = axisToSigned1000(rawJoyY, joyCenterY);

  // Read touch.
  uint16_t tx = lastTouchX;
  uint16_t ty = lastTouchY;
  uint8_t gesture = 0;
  bool active = false;
  const bool touchOk = cst816dReadTouch(tx, ty, gesture, active);
  if (touchOk) {
    lastTouchX = tx;
    lastTouchY = ty;
    lastGesture = gesture;
    touchActive = active;
  }

  // Send each gesture only once until the controller returns to gesture=0.
  uint8_t gestureToSend = 0;
  if (touchOk) {
    if (gesture == 0) {
      lastGestureSent = 0;
    } else if (gesture != lastGestureSent) {
      gestureToSend = gesture;
      lastGestureSent = gesture;
    }
  }

  // Read IMU.
  float gx = 0, gy = 0, gz = 0;
  float ax = 0, ay = 0, az = 0;
  if (imuOk) {
    sensors_event_t accel, gyro, temp;
    if (mpu.getEvent(&accel, &gyro, &temp)) {
      gx = gyro.gyro.x - gyroBiasX;
      gy = gyro.gyro.y - gyroBiasY;
      gz = gyro.gyro.z - gyroBiasZ;
      ax = accel.acceleration.x;
      ay = accel.acceleration.y;
      az = accel.acceleration.z;
    }
  }

  // Packet at 50 Hz.
  if (now - lastSendMs >= 20) {
    lastSendMs = now;

    ControllerPacket p{};
    p.magic = 0xC0DE;
    p.version = 1;
    p.mode = currentMode;
    p.buttons = buttons;
    p.gesture = gestureToSend;
    p.seq = ++sequenceNumber;
    p.joy_x = joyX;
    p.joy_y = joyY;
    p.touch_x = lastTouchX;
    p.touch_y = lastTouchY;
    p.gyro_x = clampI16(lroundf(gx * 1000.0f));
    p.gyro_y = clampI16(lroundf(gy * 1000.0f));
    p.gyro_z = clampI16(lroundf(gz * 1000.0f));
    p.accel_x = clampI16(lroundf(ax * 100.0f));
    p.accel_y = clampI16(lroundf(ay * 100.0f));
    p.accel_z = clampI16(lroundf(az * 100.0f));
    p.uptime_ms = now;

    esp_err_t err = esp_now_send(
      BROADCAST_MAC,
      reinterpret_cast<const uint8_t *>(&p),
      sizeof(p));

    if (err == ESP_OK) {
      ++sendCount;
    }

    if (now - lastPacketPrintMs >= 1000) {
      lastPacketPrintMs = now;
      Serial.printf(
        "SEQ=%lu mode=%u btn=0x%02X joy=(%d,%d) touch=(%u,%u) g=%u send=%s\n",
        static_cast<unsigned long>(p.seq),
        p.mode,
        p.buttons,
        p.joy_x,
        p.joy_y,
        p.touch_x,
        p.touch_y,
        p.gesture,
        err == ESP_OK ? "OK" : "FAIL");
    }

    if (now - lastUiMs >= 250) {
      lastUiMs = now;
      drawLCD(p, true, touchOk);
      drawOLED(p, true);
    }
  }

  delay(1);
}
