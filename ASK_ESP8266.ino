#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <RCSwitch.h>

const char* ssid = "ASK";
const char* password = "";

const int RECEIVER_PIN = 5;
const int TRANSMITTER_PIN = 14;

const int EEPROM_SIZE = 4096;
const int MAX_CODES = 100;
const int EEPROM_VERSION = 2;

struct CodeRecord {
  unsigned long code;
  int bitLength;
  int protocol;
  unsigned int delay;
  char name[32];
  boolean valid;
};

ESP8266WebServer server(80);
RCSwitch rfReceiver = RCSwitch();
RCSwitch rfTransmitter = RCSwitch();
CodeRecord codes[MAX_CODES];
int codeCount = 0;

unsigned long lastReceiveTime = 0;

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  pinMode(RECEIVER_PIN, INPUT);
  pinMode(TRANSMITTER_PIN, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  
  rfReceiver.enableReceive(RECEIVER_PIN);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  int version;
  EEPROM.get(EEPROM_SIZE - 4, version);
  if (version != EEPROM_VERSION) {
    for (int i = 0; i < MAX_CODES; i++) {
      codes[i].valid = false;
    }
    saveCodesToEEPROM();
    EEPROM.put(EEPROM_SIZE - 4, EEPROM_VERSION);
    EEPROM.commit();
  } else {
    loadCodesFromEEPROM();
  }
  
  setupWebServer();
  server.begin();
}

void loop() {
  if (rfReceiver.available()) {
    unsigned long value = rfReceiver.getReceivedValue();
    if (value != 0) {
      unsigned long currentTime = millis();
      if (currentTime - lastReceiveTime > 100) {
        lastReceiveTime = currentTime;
        int bitLength = rfReceiver.getReceivedBitlength();
        int protocol = rfReceiver.getReceivedProtocol();
        unsigned int delayVal = rfReceiver.getReceivedDelay();
        
        saveCode(value, bitLength, protocol, delayVal);
        
        digitalWrite(2, LOW);
        delay(20);
        digitalWrite(2, HIGH);
      }
    }
    rfReceiver.resetAvailable();
  }
  
  server.handleClient();
}

void saveCode(unsigned long code, int bitLength, int protocol, unsigned int delayVal) {
  for (int i = 0; i < MAX_CODES; i++) {
    if (codes[i].valid && codes[i].code == code) {
      return;
    }
  }
  for (int i = 0; i < MAX_CODES; i++) {
    if (!codes[i].valid) {
      codes[i].code = code;
      codes[i].bitLength = bitLength;
      codes[i].protocol = protocol;
      codes[i].delay = delayVal;
      sprintf(codes[i].name, "Code_%lu", code);
      codes[i].valid = true;
      saveCodesToEEPROM();
      return;
    }
  }
  static int nextSlot = 0;
  codes[nextSlot].code = code;
  codes[nextSlot].bitLength = bitLength;
  codes[nextSlot].protocol = protocol;
  codes[nextSlot].delay = delayVal;
  sprintf(codes[nextSlot].name, "Code_%lu", code);
  codes[nextSlot].valid = true;
  nextSlot = (nextSlot + 1) % MAX_CODES;
  saveCodesToEEPROM();
}

void sendCode(int index) {
  if (!codes[index].valid) return;
  
  rfTransmitter.enableTransmit(TRANSMITTER_PIN);
  rfTransmitter.setProtocol(codes[index].protocol);
  if (codes[index].delay > 0) {
    rfTransmitter.setPulseLength(codes[index].delay);
  }
  
  unsigned long startTime = millis();
  
  while (millis() - startTime < 1000) {
    rfTransmitter.send(codes[index].code, codes[index].bitLength);
    delay(10);
  }
  
  rfTransmitter.disableTransmit();
  
  digitalWrite(2, LOW);
  delay(100);
  digitalWrite(2, HIGH);
}

void deleteCode(int index) {
  if (index >= 0 && index < MAX_CODES && codes[index].valid) {
    codes[index].valid = false;
    saveCodesToEEPROM();
  }
}

void clearAllCodes() {
  for (int i = 0; i < MAX_CODES; i++) {
    codes[i].valid = false;
  }
  saveCodesToEEPROM();
}

void loadCodesFromEEPROM() {
  int addr = 0;
  for (int i = 0; i < MAX_CODES; i++) {
    EEPROM.get(addr, codes[i]);
    addr += sizeof(CodeRecord);
  }
  codeCount = 0;
  for (int i = 0; i < MAX_CODES; i++) {
    if (codes[i].valid) {
      codeCount++;
    }
  }
}

void saveCodesToEEPROM() {
  int addr = 0;
  for (int i = 0; i < MAX_CODES; i++) {
    EEPROM.put(addr, codes[i]);
    addr += sizeof(CodeRecord);
  }
  EEPROM.commit();
  codeCount = 0;
  for (int i = 0; i < MAX_CODES; i++) {
    if (codes[i].valid) codeCount++;
  }
}

void setupWebServer() {
  server.on("/", []() {
    String html = generateHTML();
    server.send(200, "text/html", html);
  });
  
  server.on("/api/codes", HTTP_GET, []() {
    String json = "[";
    for (int i = 0; i < MAX_CODES; i++) {
      if (codes[i].valid) {
        if (json.length() > 1) json += ",";
        json += "{\"index\":" + String(i) +
                ",\"code\":\"" + String(codes[i].code) +
                "\",\"bitLength\":" + String(codes[i].bitLength) +
                ",\"name\":\"" + String(codes[i].name) + "\"}";
      }
    }
    json += "]";
    server.send(200, "application/json", json);
  });
  
  server.on("/api/send", HTTP_POST, []() {
    if (server.hasArg("index")) {
      int index = server.arg("index").toInt();
      if (index >= 0 && index < MAX_CODES && codes[index].valid) {
        sendCode(index);
        server.send(200, "text/plain", "Code sent continuously for 1 second");
      } else {
        server.send(404, "text/plain", "Code not found");
      }
    } else {
      server.send(400, "text/plain", "Missing index parameter");
    }
  });
  
  server.on("/api/delete", HTTP_POST, []() {
    if (server.hasArg("index")) {
      int index = server.arg("index").toInt();
      if (index >= 0 && index < MAX_CODES && codes[index].valid) {
        deleteCode(index);
        server.send(200, "text/plain", "Code deleted successfully");
      } else {
        server.send(404, "text/plain", "Code not found");
      }
    } else {
      server.send(400, "text/plain", "Missing index parameter");
    }
  });
  
  server.on("/api/clear", HTTP_POST, []() {
    clearAllCodes();
    server.send(200, "text/plain", "All codes cleared");
  });
  
  server.on("/api/add", HTTP_POST, []() {
    if (server.hasArg("code") && server.hasArg("name")) {
      String codeStr = server.arg("code");
      String nameStr = server.arg("name");
      unsigned long code = codeStr.toInt();
      
      for (int i = 0; i < MAX_CODES; i++) {
        if (!codes[i].valid) {
          codes[i].code = code;
          codes[i].bitLength = 24;
          codes[i].protocol = 1;
          codes[i].delay = 0;
          nameStr.toCharArray(codes[i].name, 32);
          codes[i].valid = true;
          saveCodesToEEPROM();
          server.send(200, "text/plain", "Code added successfully");
          return;
        }
      }
      server.send(500, "text/plain", "Memory full");
    } else {
      server.send(400, "text/plain", "Missing code or name parameter");
    }
  });
  
  server.on("/api/rename", HTTP_POST, []() {
    if (server.hasArg("index") && server.hasArg("name")) {
      int index = server.arg("index").toInt();
      String nameStr = server.arg("name");
      if (index >= 0 && index < MAX_CODES && codes[index].valid) {
        nameStr.toCharArray(codes[index].name, 32);
        saveCodesToEEPROM();
        server.send(200, "text/plain", "Code renamed successfully");
      } else {
        server.send(404, "text/plain", "Code not found");
      }
    } else {
      server.send(400, "text/plain", "Missing index or name parameter");
    }
  });
}

String generateHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=yes">
    <title>ASK Recorder</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        body {
            background: #1a1a1a;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .header {
            background: #2d2d2d;
            padding: 20px;
            border-radius: 15px;
            margin-bottom: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            text-align: center;
            border: 1px solid #404040;
        }
        .header h1 {
            color: #ffffff;
            font-size: 24px;
            margin-bottom: 10px;
        }
        .stats {
            display: flex;
            justify-content: center;
            gap: 15px;
            margin-top: 15px;
        }
        .stat-item {
            text-align: center;
            background: #363636;
            padding: 10px 20px;
            border-radius: 10px;
            border-left: 4px solid #00ff00;
        }
        .stat-value {
            font-size: 32px;
            font-weight: bold;
            color: #00ff00;
        }
        .stat-label {
            color: #cccccc;
            font-size: 14px;
        }
        .code-list {
            background: #2d2d2d;
            border-radius: 15px;
            padding: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            margin-bottom: 20px;
            border: 1px solid #404040;
        }
        .code-item {
            background: #363636;
            border-radius: 10px;
            padding: 15px;
            margin-bottom: 10px;
            border-left: 4px solid #00ff00;
            animation: slideIn 0.3s ease;
        }
        @keyframes slideIn {
            from {
                opacity: 0;
                transform: translateY(-10px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        .code-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        .code-value {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            font-size: 18px;
            font-weight: bold;
            color: #00ff00;
            direction: ltr;
        }
        .code-name {
            background: #00ff00;
            color: #000000;
            padding: 5px 10px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: bold;
        }
        .code-actions {
            display: flex;
            gap: 10px;
            margin-top: 10px;
        }
        .btn {
            flex: 1;
            padding: 12px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 5px;
        }
        .inpt {
            flex: 1;
            padding: 12px;
            width: 100%;
            border-radius: 8px;
            font-size: 16px;
            transition: all 0.3s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 5px;
            margin-top: 4px;
            border-width: 1px;
        }
        .btn-send {
            background: #28a745;
            color: white;
        }
        .btn-send:hover {
            background: #218838;
        }
        .btn-delete {
            background: #dc3545;
            color: white;
        }
        .btn-delete:hover {
            background: #c82333;
        }
        .btn-rename {
            background: #ffc107;
            color: #000000;
        }
        .btn-rename:hover {
            background: #e0a800;
        }
        .btn-clear {
            background: #6c757d;
            color: white;
            width: 100%;
            margin-top: 10px;
        }
        .btn-clear:hover {
            background: #5a6268;
        }
        .empty-state {
            text-align: center;
            padding: 40px;
            color: #999999;
        }
        .toast {
            position: fixed;
            bottom: 20px;
            left: 50%;
            transform: translateX(-50%);
            background: #333333;
            color: white;
            padding: 12px 24px;
            border-radius: 25px;
            font-size: 14px;
            opacity: 0;
            transition: opacity 0.3s ease;
            z-index: 1000;
            border: 1px solid #00ff00;
        }
        .toast.show {
            opacity: 1;
        }
        .loading {
            text-align: center;
            padding: 20px;
            color: #ffffff;
        }
        .loading-spinner {
            border: 3px solid #404040;
            border-top: 3px solid #00ff00;
            border-radius: 50%;
            width: 40px;
            height: 40px;
            animation: spin 1s linear infinite;
            margin: 0 auto 10px;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        .refresh-btn {
            background: transparent;
            border: 2px solid #00ff00;
            color: #00ff00;
            padding: 8px 16px;
            border-radius: 20px;
            cursor: pointer;
            font-size: 14px;
            margin-top: 10px;
            transition: all 0.3s ease;
        }
        .refresh-btn:hover {
            background: #00ff00;
            color: #000000;
        }
        .status-badge {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 12px;
            font-size: 11px;
            background: #404040;
            color: #00ff00;
            margin-left: 5px;
        }
        .modal {
          display: none;
          position: fixed;
          z-index: 1;
          left: 0;
          top: 0;
          width: 100%;
          height: 100%;
          overflow: auto;
          background-color: rgba(0,0,0,0.4);
        }
        .modal-content {
          background-color: #fefefe;
          margin: 15% auto;
          padding: 20px;
          border: 1px solid #888;
          width: 80%;
          max-width: 500px;
          box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
        }
        .close {
          color: #aaa;
          float: right;
          font-size: 28px;
          font-weight: bold;
        }
        .close:hover,
        .close:focus {
          color: black;
          text-decoration: none;
          cursor: pointer;
        }
        .live-indicator {
            display: inline-block;
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background-color: #00ff00;
            animation: pulse 1s infinite;
            margin-left: 10px;
        }
        @keyframes pulse {
            0% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.5; transform: scale(1.2); }
            100% { opacity: 1; transform: scale(1); }
        }
        @media (max-width: 600px) {
          .modal-content {
            width: 95%;
          }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>📻 ASK Recorder <span class="live-indicator"></span></h1>
            <div class="stats">
                <div class="stat-item">
                    <div class="stat-value" id="totalCount">0</div>
                    <div class="stat-label">Saved Codes</div>
                </div>
            </div>
            <button class="refresh-btn" onclick="loadCodes()">🔄 Refresh</button>
            <button class="refresh-btn" onclick="showAddCodeDialog()">➕ Add Code</button>
        </div>
        <div class="code-list">
            <div id="codesContainer">
                <div class="loading">
                    <div class="loading-spinner"></div>
                    <p>Loading...</p>
                </div>
            </div>
            <button class="btn btn-clear" onclick="clearAllCodes()">🗑️ Clear All Codes</button>
        </div>
    </div>
    <div id="addCodeDialog" class="modal">
      <div class="modal-content">
        <span class="close" onclick="closeAddCodeDialog()">&times;</span>
        <h2 style="padding-bottom: 25px;">Add Code Manually</h2>
        <label for="codeInput">Enter Code:</label>
        <input class="inpt" type="text" id="codeInput" name="codeInput"><br><br>
        <label for="codeNameInput">Enter Name:</label>
        <input class="inpt" type="text" id="codeNameInput" name="codeNameInput"><br>
        <button class="btn btn-clear" onclick="addCodeManually()" style="margin-top: 25px;">Save Code</button>
      </div>
    </div>
    <div id="toast" class="toast"></div>
    <script>
        function showToast(message, duration = 3000) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.classList.add('show');
            setTimeout(() => {
                toast.classList.remove('show');
            }, duration);
        }
        
        function showAddCodeDialog() {
            document.getElementById("addCodeDialog").style.display = "block";
            document.getElementById("codeInput").focus();
        }
        
        function closeAddCodeDialog() {
            document.getElementById("addCodeDialog").style.display = "none";
        }
        
        async function addCodeManually() {
            const codeInput = document.getElementById("codeInput").value;
            const nameInput = document.getElementById("codeNameInput").value;
            if (!codeInput || !nameInput) {
                showToast('Please enter both code and name');
                return;
            }
            try {
                const response = await fetch('/api/add', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'code=' + codeInput + '&name=' + encodeURIComponent(nameInput)
                });
                if (response.ok) {
                    showToast('✅ Code added successfully');
                    loadCodes();
                    closeAddCodeDialog();
                    document.getElementById("codeInput").value = "";
                    document.getElementById("codeNameInput").value = "";
                } else {
                    showToast('❌ Error adding code');
                }
            } catch (error) {
                showToast('❌ Server connection error');
            }
        }
        
        async function renameCode(index) {
            const newName = prompt("Enter new name for the code:");
            if (!newName || newName.trim() === "") return;
            try {
                const response = await fetch('/api/rename', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'index=' + index + '&name=' + encodeURIComponent(newName.trim())
                });
                if (response.ok) {
                    showToast('✅ Code renamed successfully');
                    loadCodes();
                } else {
                    showToast('❌ Error renaming code');
                }
            } catch (error) {
                showToast('❌ Server connection error');
            }
        }
        
        async function loadCodes() {
            try {
                const response = await fetch('/api/codes');
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                const codes = await response.json();
                document.getElementById('totalCount').textContent = codes.length;
                displayCodes(codes);
            } catch (error) {
                console.error('Error loading codes:', error);
                showToast('Error loading codes');
                const container = document.getElementById('codesContainer');
                container.innerHTML = '<div class="empty-state"><p>Error loading codes. Please refresh.</p></div>';
            }
        }
        
        function displayCodes(codes) {
            const container = document.getElementById('codesContainer');
            if (codes.length === 0) {
                container.innerHTML = `
                    <div class="empty-state">
                        <div style="font-size: 48px; margin-bottom: 15px;">📡</div>
                        <p>No codes received yet</p>
                        <small>Press remote button - instant capture!</small>
                    </div>
                `;
                return;
            }
            let html = '';
            codes.forEach(code => {
                html += `
                    <div class="code-item" id="code-${code.index}">
                        <div class="code-header">
                            <span class="code-value">${escapeHtml(code.name)}</span>
                            <span class="code-name">RF</span>
                        </div>
                        <div style="margin-bottom: 10px; font-size: 12px; color: #999999;">
                            <span class="status-badge">Code: ${code.code}</span>
                            <span class="status-badge">${code.bitLength} bits</span>
                        </div>
                        <div class="code-actions">
                            <button class="btn btn-send" onclick="sendCode(${code.index})">
                                📤 Send
                            </button>
                            <button class="btn btn-rename" onclick="renameCode(${code.index})">
                                ✏️ Rename
                            </button>
                            <button class="btn btn-delete" onclick="deleteCode(${code.index})">
                                🗑️ Delete
                            </button>
                        </div>
                    </div>
                `;
            });
            container.innerHTML = html;
        }
        
        function escapeHtml(text) {
            if (!text) return '';
            return text.replace(/[&<>]/g, function(m) {
                if (m === '&') return '&amp;';
                if (m === '<') return '&lt;';
                if (m === '>') return '&gt;';
                return m;
            });
        }
        
        async function sendCode(index) {
            const btn = event.target;
            const originalText = btn.innerHTML;
            btn.innerHTML = '📤 Sending...';
            btn.disabled = true;
            
            try {
                const response = await fetch('/api/send', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'index=' + index
                });
                if (response.ok) {
                    showToast('✅ Code sent continuously for 1 second');
                } else {
                    showToast('❌ Error sending code');
                }
            } catch (error) {
                showToast('❌ Server connection error');
            } finally {
                btn.innerHTML = originalText;
                btn.disabled = false;
            }
        }
        
        async function deleteCode(index) {
            if (!confirm('Are you sure you want to delete this code?')) return;
            try {
                const response = await fetch('/api/delete', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'index=' + index
                });
                if (response.ok) {
                    showToast('✅ Code deleted successfully');
                    loadCodes();
                } else {
                    showToast('❌ Error deleting code');
                }
            } catch (error) {
                showToast('❌ Server connection error');
            }
        }
        
        async function clearAllCodes() {
            if (!confirm('Are you sure you want to delete ALL codes?')) return;
            try {
                const response = await fetch('/api/clear', {
                    method: 'POST'
                });
                if (response.ok) {
                    showToast('✅ All codes cleared');
                    loadCodes();
                } else {
                    showToast('❌ Error clearing codes');
                }
            } catch (error) {
                showToast('❌ Server connection error');
            }
        }
        
        setInterval(loadCodes, 3000);
        loadCodes();
    </script>
</body>
</html>
)rawliteral";
}
