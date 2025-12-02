#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>


// 🔥 CONFIGURAÇÕES DO FIREBASE
#define API_KEY       "AIzaSyDK6BihUGbjJAUCog5ar10xrLIEoW3DrpU"
#define DATABASE_URL  "https://luzdaarea-default-rtdb.firebaseio.com"

// 🔐 LOGIN DO USUÁRIO (Firebase Authentication)
#define USER_EMAIL     "mariano@gmail.com"
#define USER_PASSWORD  "mariano75"
// ⚠ NÃO coloque barra no final

// -----------------------------
// PINOS 
#define PINO_LDR A0
#define RELE         D2   
#define LED_WIFI     D6
#define LED_FIREBASE D5
#define PINO_DHT     D4
#define TIPO_DHT     DHT11

// -----------------------------
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);
DHT dht(PINO_DHT, TIPO_DHT);

// -----------------------------
unsigned long tempoInicio = 0;
unsigned long ultimaAtualizacao = 0;
unsigned long ultimaTrocaStatus = 0;
unsigned long ultimaPiscaFirebase = 0;
unsigned long ultimaLeituraDHT = 0;
unsigned long ultimaLeituraLDR = 0;

const unsigned long INTERVALO_ATUALIZACAO = 10000;
const unsigned long INTERVALO_STATUS = 3000;
const unsigned long INTERVALO_PISCA_FIREBASE = 500;
const unsigned long INTERVALO_DHT = 5000; // Ler sensor a cada 5 segundos
const unsigned long INTERVALO_LDR = 3000; // Ler LDR a cada 3 segundos

bool estadoFirebaseLED = false;
int statusAtual = 0;
int ultimoStatusEnviado = -1;
bool estadoAnteriorRele = HIGH;
String ultimoModo = "";

String horarioLigar1 = "00:00", horarioDesligar1 = "00:00";
String horarioLigar2 = "00:00", horarioDesligar2 = "00:00";

char data_str[11];

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
    String estadoStr = (digitalRead(RELE) == LOW) ? "0" : "1"; //string "rele" = 0  
  Firebase.RTDB.setString(&fbdo, "/rele", estadoStr);
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

void lerEnviarDHT() {
  if (!Firebase.ready()) return;

  // Ler umidade
  float h = dht.readHumidity();
  // Ler temperatura em Celsius
  float t = dht.readTemperature();
  
  // Verificar se as leituras são válidas
  if (isnan(h) || isnan(t)) {
    Serial.println("Falha na leitura do sensor DHT!");
    return;
  }
  
  // Enviar para Firebase
  Firebase.RTDB.setFloat(&fbdo, "/Humidity", h);
  Firebase.RTDB.setFloat(&fbdo, "/Temperature", t);
  
  Serial.print("Umidade: ");
  Serial.print(h);
  Serial.print(" % | Temperatura: ");
  Serial.print(t);
  Serial.println(" °C");
}

void lerEnviarLDR() {
  if (!Firebase.ready()) return;

  // Ler valor do LDR (0-1023)
  int valorLDR = analogRead(PINO_LDR);
  
  // Enviar para Firebase
  Firebase.RTDB.setInt(&fbdo, "/LDR", valorLDR);
  
  Serial.print("LDR: ");
  Serial.println(valorLDR);
}

// -----------------------------
// SETUP
// -----------------------------
void setup() {
  Serial.begin(115200);

  pinMode(RELE, OUTPUT);    digitalWrite(RELE, HIGH);
  pinMode(LED_WIFI, OUTPUT);     digitalWrite(LED_WIFI, HIGH);
  pinMode(LED_FIREBASE, OUTPUT); digitalWrite(LED_FIREBASE, HIGH);

  // Configurar pino LDR como entrada
  pinMode(PINO_LDR, INPUT);

  // Inicializar sensor DHT
  dht.begin();

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

  timeClient.begin();
  while (!timeClient.update()) timeClient.forceUpdate();

  tempoInicio = millis();
  enviarHorarioAtual();
}

// -----------------------------
// LOOP
// -----------------------------
void loop() {
  unsigned long agora = millis();

  digitalWrite(LED_WIFI, (WiFi.status() == WL_CONNECTED) ? LOW : HIGH);

  // Atualizar tempo conectado
  if (agora - ultimaAtualizacao >= INTERVALO_ATUALIZACAO) {
    ultimaAtualizacao = agora;
    atualizarTempoConectado();
  }

  // Status heartbeat
  if (agora - ultimaTrocaStatus >= INTERVALO_STATUS) {
    ultimaTrocaStatus = agora;
    statusAtual = !statusAtual;
    if (statusAtual != ultimoStatusEnviado) {
      Firebase.RTDB.setInt(&fbdo, "/Status", statusAtual);
      ultimoStatusEnviado = statusAtual;
    }
  }

  // Ler e enviar dados do DHT
  if (agora - ultimaLeituraDHT >= INTERVALO_DHT) {
    ultimaLeituraDHT = agora;
    lerEnviarDHT();
  }

  // Ler e enviar dados do LDR
  if (agora - ultimaLeituraLDR >= INTERVALO_LDR) {
    ultimaLeituraLDR = agora;
    lerEnviarLDR();
  }

  if (Firebase.ready()) {
    piscarLED(LED_FIREBASE, estadoFirebaseLED, ultimaPiscaFirebase, INTERVALO_PISCA_FIREBASE);

    // Ler agendamentos
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
    String sManual;

    if (lerStringFirebase("/ligarDesligar", sManual)) {
      if (sManual == "0" || sManual == "1") {
        controleManual = true;
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

    // Controle do relé
    String s;
    if (lerStringFirebase("/rele", s)) controlarRelePorString(RELE, s);
  }

  yield();
  delay(200);
}