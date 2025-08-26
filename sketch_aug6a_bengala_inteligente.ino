#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

const char* AP_SSID = "Bengala inteligente";
const char* AP_PASS = "bengala123";

// NodeMCU pins
const int TRIG_ESQ = 5;  // D1 = GPIO5
const int ECHO_ESQ = 4;  // D2 = GPIO4
const int TRIG_DIR = 2;  // D4 = GPIO2
const int ECHO_DIR = 0;  // D3 = GPIO0


const float VEL_SOM_CM_US = 0.0343;

ESP8266WebServer http(80);
WebSocketsServer ws(81);

// ================= HTML (removido campo de IP — conecta direto ao AP IP padrão 192.168.4.1) =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Bengala Inteligente</title>
<style>
  :root { --w: min(1000px, 92%); }
  body { margin:0; background:#0d1117; color:#fff; font-family:system-ui, Arial, sans-serif; display:flex; flex-direction:column; align-items:center; }
  header { width:100%; display:flex; justify-content:space-between; align-items:center; padding:16px; background:#161b22; }
  .left-header,.right-header { display:flex; align-items:center; gap:12px; }
  input, button { background:#0b1220; color:#fff; border:1px solid #2a3344; border-radius:10px; padding:10px 12px; cursor:pointer; }
  .dot { width:10px; height:10px; border-radius:50%; }
  .online { background:limegreen; } .offline { background:#b91c1c; }
  .radar { display:flex; flex-direction:column; gap:16px; justify-content:center; align-items:center; margin:24px auto; width:var(--w); }
  .sensor { width:100%; background:#161b22; border-radius:16px; padding:20px; text-align:left; transition:background 0.3s; }
  .sensor h3 { margin:0 0 10px; opacity:.85; }
  .sensor-value { font-size:1.6rem; font-weight:800; }
  .danger { background:#b91c1c !important; }
  .alert { margin:0 auto 16px; padding:14px; border-radius:12px; background:#21262d; width:var(--w); text-align:center; transition:background 0.3s; }
  .history { width:var(--w); margin:0 auto 30px; padding:16px; background:#161b22; border-radius:12px; }
  .history ul { margin:0; padding:0; list-style:none; }
  .history li { padding:4px 0; }
</style>
</head>
<body>
<header>
  <div class="left-header">
    <strong>👨‍🦯 Bengala Inteligente</strong>
    <span id="statusTxt" class="status">
      <span id="dot" class="dot offline"></span>
      <small>Offline</small>
    </span>
  </div>
  <div class="right-header">
    <button id="btnConnect">Conectar</button>
    <button id="btnVoice">Ativar voz 🔊</button>
  </div>
</header>

<div class="radar">
  <div id="sensor-esq" class="sensor">
    <h3>Sensor Esquerdo</h3>
    <div id="valor-esq" class="sensor-value">-- cm</div>
    <div id="qualidade-esq" style="font-size: 0.8rem; margin-top: 8px;">Aguardando…</div>
  </div>
  <div id="sensor-dir" class="sensor">
    <h3>Sensor Direito</h3>
    <div id="valor-dir" class="sensor-value">-- cm</div>
    <div id="qualidade-dir" style="font-size: 0.8rem; margin-top: 8px;">Aguardando…</div>
  </div>
</div>

<div id="alerta" class="alert">🔌 Conecte ao Wi-Fi "<strong>Coringa</strong>" (senha: eutono3g) e abra: <strong>http://192.168.4.1/</strong></div>

<div class="history">
  <h3>Histórico</h3>
  <ul id="historico"></ul>
</div>

<script>
  // ===== CONFIGURAÇÃO =====
  const THRESHOLD_CM = 80;
  const FRONT_TOLERANCE_CM = 15;

  // ===== Referências aos elementos HTML =====
  const btnConnect = document.getElementById('btnConnect');
  const btnVoice = document.getElementById('btnVoice');
  const dot = document.getElementById('dot');
  const statusTxt = document.getElementById('statusTxt').lastElementChild;
  const alerta = document.getElementById('alerta');

  const vEsq = document.getElementById('valor-esq');
  const vDir = document.getElementById('valor-dir');
  const sEsq = document.getElementById('sensor-esq');
  const sDir = document.getElementById('sensor-dir');
  const qEsq = document.getElementById('qualidade-esq');
  const qDir = document.getElementById('qualidade-dir');
  const historico = document.getElementById('historico');

  // ===== WebSocket =====
  let ws;
  function wsUrl() { return 'ws://192.168.4.1:81/'; } // conecta direto ao AP IP padrão
  btnConnect.onclick = () => connect();

  function connect() {
      const url = wsUrl();
      try { ws && ws.close(); } catch (e) {}
      ws = new WebSocket(url);
      ws.onopen = () => setOnline(true, 'Online');
      ws.onmessage = e => handleData(e.data);
      ws.onclose = () => {
          setOnline(false, 'Offline');
          setTimeout(connect, 1500);
      };
      ws.onerror = () => ws.close();
  }

  // tenta conectar automaticamente
  window.addEventListener('load', () => setTimeout(connect, 800));

  function setOnline(on, msg) {
      dot.className = 'dot ' + (on ? 'online' : 'offline');
      statusTxt.textContent = msg;
  }

  // ===== Funções de voz (TTS) =====
  let ultimoAviso = "";
  let vozAtivada = false;
  let vozSelecionada = null;
  window.speechSynthesis.onvoiceschanged = () => {
      const vozes = window.speechSynthesis.getVoices() || [];
      vozSelecionada = vozes.find(v => /pt(-|_)BR/i.test(v.lang)) || vozes.find(v => v.lang.startsWith('pt')) || vozes[0] || null;
  };

  btnVoice.onclick = () => {
      vozAtivada = !vozAtivada;
      if (vozAtivada) {
          falar('Voz ativada.');
          btnVoice.textContent = 'Desativar voz 🔇';
      } else {
          window.speechSynthesis.cancel();
          btnVoice.textContent = 'Ativar voz 🔊';
      }
  };

  function falar(texto) {
      if (!vozAtivada) return;
      const msg = new SpeechSynthesisUtterance(texto);
      if (vozSelecionada) msg.voice = vozSelecionada;
      msg.lang = "pt-BR";
      window.speechSynthesis.cancel();
      window.speechSynthesis.speak(msg);
  }

  // ===== Lógica de UI e Dados =====
  function handleData(raw) {
      let obj;
      try { obj = JSON.parse(raw); } catch { return; }
      const esq = numOrNull(obj.esq), dir = numOrNull(obj.dir);
      updateSensor('esq', esq);
      updateSensor('dir', dir);
      processarSensores(esq, dir);
  }

  function numOrNull(v) { return (typeof v === 'number' && v >= 0) ? v : null; }

  function updateSensor(side, val) {
      const elV = side === 'esq' ? vEsq : vDir;
      const box = side === 'esq' ? sEsq : sDir;
      const qual = side === 'esq' ? qEsq : qDir;
      elV.textContent = val == null ? '-- cm' : `${val} cm`;

      box.classList.remove('danger');
      if (val < THRESHOLD_CM && val !== null) box.classList.add('danger');

      if (val == null) qual.textContent = 'Sem leitura';
      else qual.textContent = val > 350 ? 'Distante' : val > 150 ? 'Médio' : 'Próximo';

      const li = document.createElement('li');
      li.textContent = `${new Date().toLocaleTimeString()} — ${side === 'esq' ? 'Esq' : 'Dir'}: ${val} cm`;
      historico.prepend(li);
      while (historico.children.length > 20) historico.removeChild(historico.lastChild);
  }

  function processarSensores(esq, dir) {
      let aviso = "", visualText = "", isDanger = false;
      if (esq !== null && dir !== null && esq < THRESHOLD_CM && dir < THRESHOLD_CM) {
          const distancia = Math.min(esq, dir);
          const nivel = Math.floor(distancia / 10) * 10;
          aviso = `Obstáculo à frente a ${nivel} centímetros`;
          visualText = `🚨 Obstáculo à FRENTE a ${nivel} cm`;
          isDanger = true;
      } else if (esq !== null && esq < THRESHOLD_CM) {
          const nivel = Math.floor(esq / 10) * 10;
          aviso = `Obstáculo à esquerda a ${nivel} centímetros`;
          visualText = `⚠️ Obstáculo à ESQUERDA a ${nivel} cm`;
          isDanger = true;
      } else if (dir !== null && dir < THRESHOLD_CM) {
          const nivel = Math.floor(dir / 10) * 10;
          aviso = `Obstáculo à direita a ${nivel} centímetros`;
          visualText = `⚠️ Obstáculo à DIREITA a ${nivel} cm`;
          isDanger = true;
      } else {
          aviso = "Caminho livre";
          visualText = '✅ Área livre';
          isDanger = false;
      }
      alerta.textContent = visualText;
      alerta.style.backgroundColor = isDanger ? '#b91c1c' : '#21262d';
      if (aviso !== ultimoAviso) {
          falar(aviso);
          ultimoAviso = aviso;
      }
  }
</script>
</body>
</html>
)rawliteral";
// ================================================================================================

long medirCm(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000); // 30ms ~ 5m
  if (dur == 0) return -1;
  long cm = (long)(dur * VEL_SOM_CM_US / 2.0);
  if (cm > 400) cm = 400;
  return cm;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // atualmente o cliente só recebe mensagens do ESP
  if (type == WStype_TEXT) {
    // se quiser processar algo vindo do browser, faz aqui
    // Serial.printf("WS msg from %u: %s\n", num, (char*)payload);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_ESQ, OUTPUT); pinMode(ECHO_ESQ, INPUT);
  pinMode(TRIG_DIR, OUTPUT); pinMode(ECHO_DIR, INPUT);

  // Configura AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP(); // normalmente 192.168.4.1
  Serial.printf("AP criado: %s / IP: %s\n", AP_SSID, myIP.toString().c_str());
  Serial.println("Conecte seu celular ao Wi-Fi e abra: http://192.168.4.1/");

  // Rotas HTTP
  http.on("/", []() {
    http.send_P(200, "text/html", INDEX_HTML);
  });
  http.onNotFound([]() {
    http.send(404, "text/plain", "404: Not found");
  });
  http.begin();

  // WebSocket
  ws.begin();
  ws.onEvent(webSocketEvent);
}

void loop() {
  http.handleClient();
  ws.loop();

  static unsigned long last = 0;
  if (millis() - last >= 500) {
    last = millis();
    long esq = medirCm(TRIG_ESQ, ECHO_ESQ);
    long dir = medirCm(TRIG_DIR, ECHO_DIR);
    String json = "{\"esq\":" + String(esq) + ",\"dir\":" + String(dir) + "}";
    ws.broadcastTXT(json);
    Serial.println(json);
  }
}
