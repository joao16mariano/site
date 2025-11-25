/*
luzdaarea100@gmail.com
Oaojonairamw3yv@
https://luzdaarea-default-rtdb.firebaseio.com/
credenciais:
mariano@gmail.com
mariano75
*/

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>

// 🔥 CONFIGURAÇÕES DO FIREBASE
#define API_KEY       "AIzaSyAIRUVzpMW2sHSE3TyDBCYsQqNatjzl7OE"
#define DATABASE_URL  "https://portao-85a3c-default-rtdb.firebaseio.com"

// 🔐 LOGIN DO USUÁRIO (Firebase Authentication)
#define USER_EMAIL     "mariano@gmail.com"
#define USER_PASSWORD  "mariano75"


// -----------------------------
// PINOS (conforme você informou)
#define RELE         D2   
#define LED_WIFI     D6
#define LED_FIREBASE D8
#define RELE1        D4   
#define RELE2        D3   
#define SERVO_CAMERA D1   

// ➕ Pinos novos para o pisca do servo
#define LED_SERVO1   D7   
#define LED_SERVO2   D5   

// -----------------------------
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

Servo servoCamera;

// -----------------------------
unsigned long tempoInicio = 0;
unsigned long ultimaAtualizacao = 0;
unsigned long ultimaTrocaStatus = 0;
unsigned long ultimaPiscaFirebase = 0;

const unsigned long INTERVALO_ATUALIZACAO = 10000;
const unsigned long INTERVALO_STATUS = 3000;
const unsigned long INTERVALO_PISCA_FIREBASE = 500;

bool estadoFirebaseLED = false;
int statusAtual = 0;
int ultimoStatusEnviado = -1;
bool estadoAnteriorRele = HIGH;
String ultimoModo = "";

String horarioLigar1 = "00:00", horarioDesligar1 = "00:00";
String horarioLigar2 = "00:00", horarioDesligar2 = "00:00";

char data_str[11];
int ultimoAnguloServo = -1;

// -----------------------------
// Variáveis do pisca do servo
unsigned long ultimaTrocaServoLED1 = 0;
unsigned long ultimaTrocaServoLED2 = 0;

bool estadoServoLED1 = false;
bool estadoServoLED2 = false;

// -----------------------------
// FUNÇÕES
// -----------------------------

void piscarLED(uint8_t pino, bool &estado, unsigned long &ultimaTroca, unsigned long intervalo) {
  unsigned long agora = millis();
  if (agora - ultimaTroca >= intervalo) {
    ultimaTroca = agora;
    estado = !estado;
    digitalWrite(pino, estado ? HIGH : LOW);
  }
}

void controlarRelePorString(uint8_t pino, const String &valorStr) {
  if (valorStr == "0") digitalWrite(pino, LOW);
  else digitalWrite(pino, HIGH);
}

void atualizarNoFirebaseEstadoReles() {
  String estadoStr = (digitalRead(RELE) == LOW) ? "0" : "1";
  Firebase.RTDB.setString(&fbdo, "/led2", estadoStr);
}

void enviarHorarioAtual() {
  time_t rawTime = timeClient.getEpochTime();
  struct tm *timeInfo = localtime(&rawTime);

  sprintf(data_str, "%02d-%02d-%04d", timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);

  Firebase.RTDB.setString(&fbdo, "/data", data_str);
  Firebase.RTDB.setInt(&fbdo, "/hora", timeInfo->tm_hour);
  Firebase.RTDB.setInt(&fbdo, "/minuto", timeInfo->tm_min);
  Firebase.RTDB.setInt(&fbdo, "/segundo", timeInfo->tm_sec);
}

void atualizarTempoConectado() {
  unsigned long tempoConectado = (millis() - tempoInicio) / 1000;
  Firebase.RTDB.setString(&fbdo, "/datasAtual", data_str);
  Firebase.RTDB.setInt(&fbdo, "/horasAtual", tempoConectado / 3600);
  Firebase.RTDB.setInt(&fbdo, "/minutosAtual", (tempoConectado % 3600) / 60);
  Firebase.RTDB.setInt(&fbdo, "/segundosAtual", tempoConectado % 60);
}

bool lerStringFirebase(const char *path, String &out) {
  if (Firebase.RTDB.getString(&fbdo, path)) {
    out = fbdo.stringData();
    return true;
  }
  return false;
}

bool lerIntFirebase(const char *path, int &out) {
  if (Firebase.RTDB.getInt(&fbdo, path)) {
    out = fbdo.intData();
    return true;
  }
  return false;
}

void controlarServoCamera(int angulo) {
  if (angulo < 0 || angulo > 180) return;
  servoCamera.write(angulo);
  if (Firebase.ready()) Firebase.RTDB.setInt(&fbdo, "/camera1", angulo);
}

// -----------------------------
// FUNÇÕES NOVAS — pisca servo
// -----------------------------

void piscarServoFaixa1(int angulo) {
  if (angulo >= 0 && angulo <= 90) {
    piscarLED(LED_SERVO1, estadoServoLED1, ultimaTrocaServoLED1, 300);
  } else {
    digitalWrite(LED_SERVO1, LOW);
  }
}

void piscarServoFaixa2(int angulo) {
  if (angulo >= 91 && angulo <= 180) {
    piscarLED(LED_SERVO2, estadoServoLED2, ultimaTrocaServoLED2, 300);
  } else {
    digitalWrite(LED_SERVO2, LOW);
  }
}

// -----------------------------
// SETUP
// -----------------------------
void setup() {
  Serial.begin(115200);

  pinMode(RELE, OUTPUT);    digitalWrite(RELE, HIGH);
  pinMode(RELE1, OUTPUT);   digitalWrite(RELE1, HIGH);
  pinMode(RELE2, OUTPUT);   digitalWrite(RELE2, HIGH);

  pinMode(LED_WIFI, OUTPUT);     digitalWrite(LED_WIFI, HIGH);
  pinMode(LED_FIREBASE, OUTPUT); digitalWrite(LED_FIREBASE, HIGH);

  // LEDs do servo
  pinMode(LED_SERVO1, OUTPUT); digitalWrite(LED_SERVO1, LOW);
  pinMode(LED_SERVO2, OUTPUT); digitalWrite(LED_SERVO2, LOW);

  servoCamera.attach(SERVO_CAMERA);
  // servoCamera.write(0);  // REMOVIDO

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  if (!wm.autoConnect("Controle Sala", "mariano75")) ESP.restart();

  digitalWrite(LED_WIFI, LOW);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  unsigned long inicioAuth = millis();
  while ((auth.token.uid) == "") {
    if (millis() - inicioAuth > 10000) break;
    delay(300);
  }

  // ------ RESTAURAR ÂNGULO DO SERVO ------
  int anguloSalvo = 90; 
  if (Firebase.RTDB.getInt(&fbdo, "/camera1")) {
    anguloSalvo = fbdo.intData();
  }
  ultimoAnguloServo = anguloSalvo;
  servoCamera.write(anguloSalvo);
  // ---------------------------------------

  timeClient.begin();
  while (!timeClient.update()) timeClient.forceUpdate();

  tempoInicio = millis();
  enviarHorarioAtual();

  // if (Firebase.ready()) Firebase.RTDB.setInt(&fbdo, "/camera1", 0); // REMOVIDO
}

// -----------------------------
// LOOP
// -----------------------------
void loop() {
  unsigned long agora = millis();

  digitalWrite(LED_WIFI, (WiFi.status() == WL_CONNECTED) ? LOW : HIGH);

  if (agora - ultimaAtualizacao >= INTERVALO_ATUALIZACAO) {
    ultimaAtualizacao = agora;
    atualizarTempoConectado();
  }

  if (agora - ultimaTrocaStatus >= INTERVALO_STATUS) {
    ultimaTrocaStatus = agora;
    statusAtual = !statusAtual;
    if (statusAtual != ultimoStatusEnviado) {
      Firebase.RTDB.setInt(&fbdo, "/Status", statusAtual);
      ultimoStatusEnviado = statusAtual;
    }
  }

  if (Firebase.ready()) {

    piscarLED(LED_FIREBASE, estadoFirebaseLED, ultimaPiscaFirebase, INTERVALO_PISCA_FIREBASE);

    int angulo;
    if (lerIntFirebase("/camera1", angulo)) {
      if (angulo != ultimoAnguloServo && angulo >= 0 && angulo <= 180) {
        controlarServoCamera(angulo);
        ultimoAnguloServo = angulo;
      }
    }

    piscarServoFaixa1(ultimoAnguloServo);
    piscarServoFaixa2(ultimoAnguloServo);

    String tmp;
    if (lerStringFirebase("/ligarHorario", tmp)) horarioLigar1 = tmp;
    if (lerStringFirebase("/desligarHorario", tmp)) horarioDesligar1 = tmp;
    if (lerStringFirebase("/ligarHorario2", tmp)) horarioLigar2 = tmp;
    if (lerStringFirebase("/desligarHorario2", tmp)) horarioDesligar2 = tmp;

    timeClient.update();
    char horaAtualStr[6];
    sprintf(horaAtualStr, "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
    String horaAtual = horaAtualStr;

    bool controleProgramado =
      (horaAtual >= horarioLigar1 && horaAtual < horarioDesligar1) ||
      (horaAtual >= horarioLigar2 && horaAtual < horarioDesligar2);

    bool controleManual = false;
    int estadoManual = -1;
    String sManual;

    if (lerStringFirebase("/ligarDesligar", sManual)) {
      if (sManual == "0" || sManual == "1") {
        controleManual = true;
        estadoManual = (sManual == "0") ? 0 : 1;
      }
    }

    String novoModo;

    if (controleManual) {
      controlarRelePorString(RELE, sManual);
      novoModo = "Manual";
    } else {
      controlarRelePorString(RELE, controleProgramado ? "0" : "1");
      novoModo = "Programado";

      bool atual = (digitalRead(RELE) == LOW);
      if ((atual ? LOW : HIGH) != estadoAnteriorRele) {
        estadoAnteriorRele = digitalRead(RELE);
        timeClient.update();
        enviarHorarioAtual();
      }
    }

    atualizarNoFirebaseEstadoReles();

    if (novoModo != ultimoModo) {
      Firebase.RTDB.setString(&fbdo, "/modoControle", novoModo);
      ultimoModo = novoModo;
    }

    String s;
    if (lerStringFirebase("/led", s))  controlarRelePorString(RELE1, s);
    if (lerStringFirebase("/led1", s)) controlarRelePorString(RELE2, s);
    if (lerStringFirebase("/led2", s)) controlarRelePorString(RELE,  s);
  }

  yield();
  delay(200);
}

