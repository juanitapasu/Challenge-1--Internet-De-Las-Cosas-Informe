#include <HardwareSerial.h>
#include <Wire.h>
#include <math.h>

// =====================================================
//  SENSORES:
//   - SEN0177 (PM1/PM2.5/PM10) por UART1
//   - MQ135 (gas -> "NH3_est" por curva)
//   - BME280/BMP280 (T/P/H) por I2C
//  ACTUADORES:
//   - LCD I2C 16x2
//   - LED RGB (PWM)
//   - Buzzer (alarma por estado)
// =====================================================


// ------------------------------
// BME/BMP280
// ------------------------------
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;


// ------------------------------
// LCD I2C
// ------------------------------
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7);


// ------------------------------
// LED RGB 
// ------------------------------
const int LED_R = 25;
const int LED_G = 26;
const int LED_B = 27;

const int RGB_COMMON_ANODE = 0;

// Canales PWM ESP32
const int CH_R = 0;
const int CH_G = 1;
const int CH_B = 2;


// ------------------------------
// BUZZER (CONFIGURA PIN)
// ------------------------------
const int BUZZER_PIN = 33;    
const int BUZZER_ACTIVE_HIGH = 1; 


// =====================================================
//  MODELO: K-means + Votación (KMeans / Flags / Trend)
// =====================================================
//
//  Features usadas (4):
//    x = [PM1_norm, PM25_norm, PM10_norm, NH3_norm]
//
//  1) K-means (K=3):
//     - En entrenamiento se obtuvieron 3 centroides en espacio normalizado [0..1].
//     - En el ESP32 NO "entrenamos": solo calculamos distancia al centroide más cercano.
//     - Ese cluster se mapea a un nivel: Normal / Precaución / Peligro
//
//  2) Flags (umbrales P75 y P90):
//     - Regla simple tipo "banderas":
//         si varias features > P90 -> Peligro
//         si varias features > P75 -> Precaución
//       Esto es robusto para casos donde el K-means se equivoque.
//
//  3) Trend (tendencia):
//     - Guarda una ventana de las últimas lecturas normalizadas (PM2.5 y NH3).
//     - Si está SUBIENDO (dp>0 o dn>0) y ya va por encima de P75, vota Precaución.
//     - Si además supera P90, vota Peligro.
//
//  4) Votación final (majority):
//     - FINAL = mayoría entre (voteKMeans, voteFlags, voteTrend)
//     - Esto reduce falsos positivos/negativos por ruido o outliers.
//
//  5) Causa:
///    - Se identifica si el "disparo" principal fue PM, NH3 o Mixto
//      usando el criterio de P90 (alto).
// =====================================================


// ------------------------------
// K-means + percentiles (exportados del entrenamiento)
// ------------------------------
const int N_FEAT = 4;
const char* FEAT_NAMES[4] = {"PM1","PM25","PM10","NH3"};

const int K = 3;
const float CENTROIDS[3][4] = {
  {0.067695f, 0.066557f, 0.073262f, 0.766916f},
  {0.477728f, 0.471661f, 0.430091f, 0.752588f},
  {0.124728f, 0.115632f, 0.105316f, 0.248652f},
};

const float P75[4] = {0.407268f, 0.376813f, 0.326889f, 0.826943f};
const float P90[4] = {0.528457f, 0.544942f, 0.512827f, 0.911577f};

// Mapeo cluster -> nivel (0=Normal,1=Precaucion,2=Peligro)
const int CLUSTER_LEVEL[3] = {1, 2, 0};

enum Level { NORMAL=0, PRECAUCION=1, PELIGRO=2 };
const char* levelName(Level L){
  if(L==NORMAL) return "Normal";
  if(L==PRECAUCION) return "Precaucion";
  return "Peligro";
}


// ------------------------------
// SEN0177 (DFRobot) por UART1
// ------------------------------
HardwareSerial dustSerial(1);
const int DUST_RX = 16; // ESP32 RX  <- TX sensor
const int DUST_TX = 17; // ESP32 TX  -> RX sensor
const int DUST_BAUD = 9600;

#define LENG 31
uint8_t bufSEN[LENG];


// ------------------------------
// MQ135 ADC (NH3_est por curva)
// ------------------------------
const int MQ135_PIN = 34;
const float ADC_VREF = 3.3f;
const int ADC_MAX = 4095;

const float RL = 10000.0f;
const float Ro = 140000.0f;
const float MQ_A = 60.0f;
const float MQ_B = -1.2f;


// ------------------------------
// Variables sensores (raw)
// ------------------------------
uint16_t pm1_0 = 0;
uint16_t pm2_5 = 0;
uint16_t pm10  = 0;

float nh3_ppm = 0.0f; 
float tempC = NAN;
float pres_hPa = NAN;
float humRH = NAN;


// ------------------------------
// Trend (ventana de 6)
// ------------------------------
const int WIN = 6;
float histPM25[WIN];
float histNH3[WIN];
int histCount = 0;
int histIdx = 0;


// ------------------------------
// LCD screens
// ------------------------------
int screenIndex = 0;
unsigned long lastScreenMs = 0;


// ------------------------------
// Utilidades
// ------------------------------
float clamp01(float x){
  if(x < 0.0f) return 0.0f;
  if(x > 1.0f) return 1.0f;
  return x;
}
float sqf(float x){ return x*x; }


// ------------------------------
// Normalización (indoor)
//  * Importante: el K-means fue entrenado con valores en 0..1.
//  * Por eso convertimos (ug/m3, ppm_est) a [0..1] usando rangos razonables.
// ------------------------------
float normPM1(float ugm3){  return clamp01(ugm3 / 150.0f); } // indoor 0..150
float normPM25(float ugm3){ return clamp01(ugm3 / 150.0f); } // indoor 0..150
float normPM10(float ugm3){ return clamp01(ugm3 / 200.0f); } // indoor 0..200
float normNH3(float ppm){   return clamp01(ppm  / 50.0f);  } // indoor 0..50 (NH3_est)


// =====================================================
// LED RGB helpers
// =====================================================
void pwmWriteRgb(int r, int g, int b){
  
  if(RGB_COMMON_ANODE){
    r = 255 - r; g = 255 - g; b = 255 - b;
  }
  ledcWrite(LED_R, r);
  ledcWrite(LED_G, g);
  ledcWrite(LED_B, b);
}

void setLedByLevel(Level L){
  // colores convencionales para un indicador:
  // Normal=verde, Precaucion=amarillo, Peligro=rojo
  if(L==NORMAL)          pwmWriteRgb(0, 255, 0);
  else if(L==PRECAUCION) pwmWriteRgb(255, 120, 0);
  else                   pwmWriteRgb(255, 0, 0);
}


// =====================================================
// Buzzer helpers (patrones no-bloqueantes)
// =====================================================
void buzzerWrite(bool on){
  if(BUZZER_ACTIVE_HIGH){
    digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(BUZZER_PIN, on ? LOW : HIGH);
  }
}

// Patrón:
//  - NORMAL: OFF
//  - PRECAUCION: beep corto cada 2s
//  - PELIGRO: beep rápido repetitivo
void buzzerPattern(Level L){
  static unsigned long t0 = 0;
  static bool state = false;

  unsigned long now = millis();

  if(L == NORMAL){
    buzzerWrite(false);
    state = false;
    t0 = now;
    return;
  }

  if(L == PRECAUCION){
    // beep 150ms cada 2000ms
    unsigned long period = 2000;
    unsigned long onTime = 150;
    unsigned long phase = now % period;
    buzzerWrite(phase < onTime);
    return;
  }

  // PELIGRO:
  // beep 200ms ON / 200ms OFF (rápido)
  unsigned long period = 400;
  unsigned long phase = now % period;
  buzzerWrite(phase < 200);
}


// =====================================================
// SEN0177 checksum + lectura
// =====================================================
bool checkValueSEN(uint8_t *thebuf, uint8_t leng) {
  // Checksum según ejemplo datasheet DFROBOT:
  // sum(buf[0..leng-3]) + 0x42 == checksum(últimos 2 bytes)
  uint16_t receiveSum = 0;
  for (int i = 0; i < (leng - 2); i++) receiveSum += thebuf[i];
  receiveSum += 0x42;
  uint16_t checksum = ((uint16_t)thebuf[leng - 2] << 8) + thebuf[leng - 1];
  return (receiveSum == checksum);
}

bool readSEN0177(uint16_t &pm1, uint16_t &pm25, uint16_t &pm10_) {
  // busca el byte 0x42 (inicio de trama)
  if (dustSerial.find(0x42)) {
    delay(100);
    size_t n = dustSerial.readBytes(bufSEN, LENG);
    if (n != LENG) return false;

    // segundo byte esperado
    if (bufSEN[0] != 0x4D) return false;

    // checksum
    if (!checkValueSEN(bufSEN, LENG)) return false;

    // PM values según el ejemplo del datasheet
    pm1  = ((uint16_t)bufSEN[3] << 8) + bufSEN[4];
    pm25 = ((uint16_t)bufSEN[5] << 8) + bufSEN[6];
    pm10_= ((uint16_t)bufSEN[7] << 8) + bufSEN[8];
    return true;
  }
  return false;
}


// =====================================================
// MQ135 -> NH3_est (curva)
// =====================================================
float readNH3ppm(){
  // lee ADC (0..4095) -> voltaje (0..3.3V)
  int adcValue = analogRead(MQ135_PIN);
  float voltage = (adcValue / (float)ADC_MAX) * ADC_VREF;

  // evita división por cero
  if (voltage < 0.01f) voltage = 0.01f;

  // modelo simplificado:
  //  Rs = RL * ((Vref - Vout) / Vout)
  //  ratio = Rs/Ro
  //  ppm = a * ratio^b
  float rs = RL * ((ADC_VREF - voltage) / voltage);
  float ratio = rs / Ro;

  float ppm = MQ_A * pow(ratio, MQ_B);
  if(ppm < 0) ppm = 0;
  return ppm;
}


// =====================================================
// VOTACIÓN
// =====================================================

// 1) Voto K-means:
//    - Calcula distancia euclídea a cada centroide
//    - Elige el más cercano => cluster
//    - Cluster se convierte en nivel (Normal/Precaucion/Peligro)
Level voteKMeans(const float x[4], int &bestCluster, float &bestDist){
  bestCluster = 0;
  bestDist = 1e9f;

  for(int k=0;k<K;k++){
    float d = 0.0f;
    for(int j=0;j<4;j++){
      d += sqf(CENTROIDS[k][j] - x[j]);
    }
    if(d < bestDist){
      bestDist = d;
      bestCluster = k;
    }
  }
  return (Level)CLUSTER_LEVEL[bestCluster];
}

// 2) Voto por banderas (P75/P90):
//    - cuenta cuántas variables están "muy altas" (>P90) o "altas" (>P75)
//    - regla simple y robusta
Level voteFlags(const float x[4]){
  int danger = 0;
  int caution = 0;

  for(int j=0;j<4;j++){
    if(x[j] > P90[j]) danger++;
    else if(x[j] > P75[j]) caution++;
  }

  // si 2 o más variables están en zona P90 => PELIGRO
  if(danger >= 2) return PELIGRO;

  // si 2 o más variables están entre P75/P90 => PRECAUCIÓN
  if((danger + caution) >= 2) return PRECAUCION;

  return NORMAL;
}

// 3) Voto tendencia (trend):
//    - mira si PM2.5 o NH3 vienen subiendo
//    - solo activa si ya están al menos por encima de P75 (alto)
Level voteTrend(){
  if(histCount < WIN) return NORMAL;

  int i_now  = (histIdx - 1 + WIN) % WIN; // última muestra
  int i_prev = (histIdx - 4 + WIN) % WIN; // hace 3 muestras (aprox 1.5s con delay 500ms)

  float pm25_now = histPM25[i_now];
  float nh3_now  = histNH3[i_now];
  float pm25_prev= histPM25[i_prev];
  float nh3_prev = histNH3[i_prev];

  float dp = pm25_now - pm25_prev; // cambio PM25
  float dn = nh3_now  - nh3_prev;  // cambio NH3

  bool pm25_high = pm25_now > P75[1];
  bool nh3_high  = nh3_now  > P75[3];

  // si hay subida y ya está alto => precaución/peligro
  if((dp > 0.0f && pm25_high) || (dn > 0.0f && nh3_high)){
    if(pm25_now > P90[1] || nh3_now > P90[3]) return PELIGRO;
    return PRECAUCION;
  }
  return NORMAL;
}

// 4) Mayoría de votos: (KMeans, Flags, Trend)
Level majority(Level a, Level b, Level c){
  int counts[3] = {0,0,0};
  counts[(int)a]++; counts[(int)b]++; counts[(int)c]++;

  int best = 0;
  if(counts[1] > counts[best]) best = 1;
  if(counts[2] > counts[best]) best = 2;
  return (Level)best;
}

// Etiqueta de causa (para explicar qué "activó" el sistema)
const char* causeLabel(const float x[4]){
  bool pm_high  = (x[1] > P90[1]) || (x[2] > P90[2]); // PM2.5 o PM10 muy altos
  bool nh3_high = (x[3] > P90[3]);                   // NH3 muy alto

  if(pm_high && nh3_high) return "Mixto";
  if(pm_high)            return "PM";
  if(nh3_high)           return "NH3";
  return "Bajo";
}


// =====================================================
// LCD rendering (4 pantallas rotativas)
// =====================================================
void lcdShow(Level fin, const char* causa){
  lcd.clear();

  if(screenIndex == 0){
    // Pantalla 0: Estado y causa
    lcd.setCursor(0,0);
    lcd.print("Estado:");
    lcd.print(levelName(fin));
    lcd.setCursor(0,1);
    lcd.print("Causa:");
    lcd.print(causa);
  }
  else if(screenIndex == 1){
    // Pantalla 1: PM1 y PM2.5
    lcd.setCursor(0,0);
    lcd.print("PM1:");
    lcd.print(pm1_0);
    lcd.print("ug");
    lcd.setCursor(0,1);
    lcd.print("PM25:");
    lcd.print(pm2_5);
    lcd.print("ug");
  }
  else if(screenIndex == 2){
    // Pantalla 2: PM10 y NH3_est
    lcd.setCursor(0,0);
    lcd.print("PM10:");
    lcd.print(pm10);
    lcd.print("ug");
    lcd.setCursor(0,1);
    lcd.print("NH3:");
    lcd.print(nh3_ppm,1);
    lcd.print("est");
  }
  else if(screenIndex == 3){
    // Pantalla 3: Temperatura y Humedad
    lcd.setCursor(0,0);
    lcd.print("Temp: ");
    if(!isnan(tempC)) lcd.print(tempC, 1);
    else lcd.print("--");
    lcd.print(" C");

    lcd.setCursor(0,1);
    lcd.print("Hum:  ");
    if(!isnan(humRH) && humRH > 0.1f){
      lcd.print(humRH, 1);
      lcd.print(" %");
    } else {
      lcd.print("--");
    }
  }
  else {
    // Pantalla 4: Temperatura y Presión
    lcd.setCursor(0,0);
    lcd.print("Temp: ");
    if(!isnan(tempC)) lcd.print(tempC, 1);
    else lcd.print("--");
    lcd.print(" C");

    lcd.setCursor(0,1);
    lcd.print("Pres: ");
    if(!isnan(pres_hPa)){
      lcd.print(pres_hPa, 1);
      lcd.print("hPa");
    } else {
      lcd.print("--");
    }
  }
}


// =====================================================
// Setup / Loop
// =====================================================
void setup(){
  Serial.begin(115200);
  delay(800);

  // I2C + LCD
  Wire.begin();
  lcd.setBacklightPin(3, POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("IOT iniciando");

  // BME/BMP280 (auto 0x76/0x77)
  bool bmeOK = bme.begin(0x76);
  if(!bmeOK) bmeOK = bme.begin(0x77);
  Serial.println(bmeOK ? "BME/BMP280 OK" : "BME/BMP280 NO detectado");

  // SEN0177 UART
  dustSerial.begin(DUST_BAUD, SERIAL_8N1, DUST_RX, DUST_TX);
  dustSerial.setTimeout(1500);

  // LED RGB PWM setup
  ledcAttach(LED_R, 5000, 8);
  ledcAttach(LED_G, 5000, 8);
  ledcAttach(LED_B, 5000, 8);
  pwmWriteRgb(0,0,0);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerWrite(false);

  // init trend
  for(int i=0;i<WIN;i++){ histPM25[i]=0; histNH3[i]=0; }

  Serial.println("Listo. LCD + RGB + Buzzer + sensores.");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sistema listo");
}

void loop(){
  // ----------------------
  // 1) Lecturas de sensores
  // ----------------------
  nh3_ppm = readNH3ppm();

  uint16_t pm1_tmp, pm25_tmp, pm10_tmp;
  if(readSEN0177(pm1_tmp, pm25_tmp, pm10_tmp)){
    pm1_0 = pm1_tmp;
    pm2_5 = pm25_tmp;
    pm10  = pm10_tmp;
  }

  float t = bme.readTemperature();
  if(!isnan(t)){
    tempC = t;
    pres_hPa = bme.readPressure() / 100.0f;
    humRH = bme.readHumidity(); // si es BMP280 puede ser NaN/0
  }

  // ----------------------
  // 2) Features normalizadas (0..1)
  // ----------------------
  float x[4];
  x[0] = normPM1((float)pm1_0);
  x[1] = normPM25((float)pm2_5);
  x[2] = normPM10((float)pm10);
  x[3] = normNH3(nh3_ppm);

  // ----------------------
  // 3) Trend update (ventana)
  // ----------------------
  histPM25[histIdx] = x[1];
  histNH3[histIdx]  = x[3];
  histIdx = (histIdx + 1) % WIN;
  if(histCount < WIN) histCount++;

  // ----------------------
  // 4) Votación (3 votos + mayoría)
  // ----------------------
  int cluster = -1;
  float dist = 0.0f;

  Level vK = voteKMeans(x, cluster, dist);
  Level vF = voteFlags(x);
  Level vT = voteTrend();

  Level fin = majority(vK, vF, vT);
  const char* causa = causeLabel(x);

  // ----------------------
  // 5) Actuadores: LED + Buzzer
  // ----------------------
  setLedByLevel(fin);
  buzzerPattern(fin);

  // ----------------------
  // 6) Serial: imprime todo
  // ----------------------
  Serial.print("PM1="); Serial.print(pm1_0);
  Serial.print(" PM25="); Serial.print(pm2_5);
  Serial.print(" PM10="); Serial.print(pm10);
  Serial.print(" NH3_est="); Serial.print(nh3_ppm, 2);

  if(!isnan(tempC)){
    Serial.print(" T="); Serial.print(tempC,1);
    Serial.print("C P="); Serial.print(pres_hPa,1);
    Serial.print("hPa H="); Serial.print(humRH,1); Serial.print("%");
  }

  Serial.print(" | x=[");
  Serial.print(x[0],3); Serial.print(",");
  Serial.print(x[1],3); Serial.print(",");
  Serial.print(x[2],3); Serial.print(",");
  Serial.print(x[3],3); Serial.print("]");

  Serial.print(" | vK="); Serial.print(levelName(vK));
  Serial.print(" vF="); Serial.print(levelName(vF));
  Serial.print(" vT="); Serial.print(levelName(vT));
  Serial.print(" FINAL="); Serial.print(levelName(fin));
  Serial.print(" causa="); Serial.print(causa);
  Serial.print(" cluster="); Serial.print(cluster);
  Serial.print(" dist="); Serial.println(dist, 6);

  // ----------------------
  // 7) LCD: rota pantallas cada 2s
  // ----------------------
  if(millis() - lastScreenMs > 2000){
    lastScreenMs = millis();
    screenIndex = (screenIndex + 1) % 5;
  }
  lcdShow(fin, causa);

  delay(500);
}