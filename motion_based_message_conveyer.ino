  #include <Wire.h>
  #include <Adafruit_Sensor.h>
  #include <Adafruit_ADXL345_U.h>
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>

  // Sensor setup
  Adafruit_ADXL345_Unified accelerometer = Adafruit_ADXL345_Unified(12345);

  // Motion variables
  String tiltDirection = "Flat";
  String tiltMessage = "No assistance needed";
  float x = 0, y = 0, z = 0;
  unsigned long lastReadTime = 0;
  const unsigned long interval = 500; // Reduced to 500ms for more responsive visualization

  // Calibration variables
  float xOffset = 0.0;
  float yOffset = 0.0;
  float zOffset = 0.0;
  bool isCalibrated = false;
  const int calibrationSamples = 10; // Number of samples to take for calibration

  // Global threshold variables
  float slightThreshold = 1.0;
  float moderateThreshold = 2.0;
  float strongThreshold = 3.5;
  float lateralThreshold = 5.0;  // DECREASED from 8.0 to 5.0

  // WiFi credentials
  const char* ssid = "Connecting....";        // CHANGE THIS TO YOUR WIFI NAME
  const char* password = "pass12345678"; // CHANGE THIS TO YOUR WIFI PASSWORD

  // Web server setup
  ESP8266WebServer server(80);

  // Motion history for display
  #define HISTORY_SIZE 10
  struct MotionRecord {
    String direction;
    String message;
    float x;
    float y;
    float z;
    unsigned long timestamp;
  };
  MotionRecord motionHistory[HISTORY_SIZE];
  int historyIndex = 0;

  void addToHistory(String dir, String msg, float xVal, float yVal, float zVal) {
    motionHistory[historyIndex].direction = dir;
    motionHistory[historyIndex].message = msg;
    motionHistory[historyIndex].x = xVal;
    motionHistory[historyIndex].y = yVal;
    motionHistory[historyIndex].z = zVal;
    motionHistory[historyIndex].timestamp = millis();
    
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  }

  void calibrateSensor() {
    float xSum = 0, ySum = 0, zSum = 0;
    
    Serial.println("Starting calibration. Keep the sensor in its resting position...");
    
    // Take multiple samples to average out noise
    for (int i = 0; i < calibrationSamples; i++) {
      sensors_event_t event;
      accelerometer.getEvent(&event);
      
      xSum += event.acceleration.x;
      ySum += event.acceleration.y;
      zSum += event.acceleration.z;
      
      delay(100); // Short delay between samples
    }
    
    // Calculate offsets (average values in resting position)
    xOffset = xSum / calibrationSamples;
    yOffset = ySum / calibrationSamples;
    
    // For z-axis, we need to handle it differently to maintain gravity detection
    // Calculate the difference from 9.8 (Earth's gravity)
    float zAvg = zSum / calibrationSamples;
    zOffset = zAvg - 9.8; // Assume 9.8 m/s² should be the z value when flat
    
    isCalibrated = true;
    
    Serial.println("Calibration complete!");
    Serial.println("X offset: " + String(xOffset));
    Serial.println("Y offset: " + String(yOffset));
    Serial.println("Z offset: " + String(zOffset));
  }

  void setup() {
    Serial.begin(9600);
    
    Serial.println("\nMotion-Based Message Conveyer");
    Serial.println("-----------------------------");
    
    // Initialize WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
    
    // Initialize accelerometer
    if (!accelerometer.begin()) {
      Serial.println("Could not find a valid ADXL345 sensor, check wiring!");
      while (1);
    }
    accelerometer.setRange(ADXL345_RANGE_2_G);
    Serial.println("ADXL345 sensor initialized successfully");
    
    // Perform calibration at startup
    Serial.println("Starting sensor calibration...");
    delay(2000); // Wait for sensor to stabilize
    calibrateSensor();
    
    // Setup server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/history", handleHistory);
    server.on("/calibrate", handleCalibrate);  // New route for calibration
    server.on("/debug", handleDebug);  // New debug endpoint
    server.onNotFound(handleNotFound);
    
    // Start server
    server.begin();
    Serial.println("HTTP server started");
    Serial.println("Open a browser and navigate to: http://" + WiFi.localIP().toString());
  }

  void loop() {
    // Handle client requests
    server.handleClient();
    
    // Read sensor at regular intervals
    unsigned long currentTime = millis();
    if (currentTime - lastReadTime >= interval) {
      lastReadTime = currentTime;
      
      // Get motion readings
      sensors_event_t event;
      accelerometer.getEvent(&event);
      
      // Apply calibration offsets
      if (isCalibrated) {
        x = event.acceleration.x - xOffset;
        y = event.acceleration.y - yOffset;
        z = event.acceleration.z - zOffset;
      } else {
        x = event.acceleration.x;
        y = event.acceleration.y;
        z = event.acceleration.z;
      }
      
      // Process motion
      processMotion();
      
      // Add to history
      addToHistory(tiltDirection, tiltMessage, x, y, z);
      
      // Serial output for debugging
      Serial.println("Direction: " + tiltDirection);
      Serial.println("Message: " + tiltMessage);
      Serial.printf("Raw X: %.2f, Y: %.2f, Z: %.2f | Calibrated X: %.2f, Y: %.2f, Z: %.2f\n", 
                  event.acceleration.x, event.acceleration.y, event.acceleration.z, x, y, z);
    }
  }

  void processMotion() {
    // Print all values for debugging
    Serial.printf("Processed values - X: %.2f, Y: %.2f, Z: %.2f\n", x, y, z);
    
    // FIRST PRIORITY: Check if device is upside down
    // CHANGED: Moved this check to the top and changed threshold from -7.0 to -6.0
    if (z < -6.0) {
      tiltDirection = "Upside down";
      tiltMessage = "No message";
      return;
    }
    
    // SECOND PRIORITY: Check for lateral (left/right) movement
    if (x > lateralThreshold) {
      tiltDirection = "Leftward";
      tiltMessage = "Physical adjustment needed";
      return;
    } 
    else if (x < -lateralThreshold) {
      tiltDirection = "Rightward";
      tiltMessage = "Water/food needed";
      return;
    }
    
    // THIRD PRIORITY: Check for vertical movement
    if (y > strongThreshold) {
      tiltDirection = "Strong upward";
      tiltMessage = "Urgent help needed";
      return;
    }
    else if (y > moderateThreshold) {
      tiltDirection = "Moderate upward";
      tiltMessage = "Urgent help needed";
      return;
    }
    else if (y < -moderateThreshold) {
      tiltDirection = "Downward";
      tiltMessage = "Discomfort alert";
      return;
    }
    else {
      tiltDirection = "Flat";
      tiltMessage = "No assistance needed";
    }
  }

  void printOrientationDebug() {
    sensors_event_t event;
    accelerometer.getEvent(&event);
    
    // Apply calibration offsets
    float calibratedX = event.acceleration.x - xOffset;
    float calibratedY = event.acceleration.y - yOffset;
    float calibratedZ = event.acceleration.z - zOffset;
    
    // Print raw and calibrated values
    Serial.println("\n--- ORIENTATION DEBUG ---");
    Serial.println("RAW VALUES:");
    Serial.printf("X: %.2f, Y: %.2f, Z: %.2f\n", 
                  event.acceleration.x, event.acceleration.y, event.acceleration.z);
    
    Serial.println("CALIBRATED VALUES:");
    Serial.printf("X: %.2f, Y: %.2f, Z: %.2f\n", calibratedX, calibratedY, calibratedZ);
    
    // Print current thresholds
    Serial.println("CURRENT THRESHOLDS:");
    Serial.printf("Lateral: %.2f, Moderate: %.2f, Strong: %.2f\n", 
                  lateralThreshold, moderateThreshold, strongThreshold);
    
    // Suggest which direction would be detected with current values
  Serial.println("CURRENT DETECTION:");
    if (abs(calibratedX) > abs(calibratedY)) {
      Serial.println("Primary axis: X (Left/Right)");
      if (calibratedX > 0) {
        Serial.println("Direction: Leaning LEFT");  // CHANGED from RIGHT to LEFT
      } else {
        Serial.println("Direction: Leaning RIGHT");  // CHANGED from LEFT to RIGHT
      }
    } else {
      Serial.println("Primary axis: Y (Up/Down)");
      if (calibratedY > 0) {
        Serial.println("Direction: Leaning UP");
      } else {
        Serial.println("Direction: Leaning DOWN");
      }
    }
    Serial.println("-------------------------");
  }

  void handleDebug() {
    printOrientationDebug();
    server.send(200, "text/plain", "Debug information printed to serial monitor");
  }

  void handleCalibrate() {
    calibrateSensor();
    
    String response = "Sensor calibrated successfully!\n";
    response += "X offset: " + String(xOffset) + "\n";
    response += "Y offset: " + String(yOffset) + "\n";
    response += "Z offset: " + String(zOffset) + "\n";
    
    server.send(200, "text/plain", response);
  }

  void handleRoot() {
  String html = R"=====(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Motion-Based Message Conveyer</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 20px;
        background-color: #f5f5f5;
      }
      .container {
        max-width: 900px;
        margin: 0 auto;
        background-color: white;
        padding: 20px;
        border-radius: 10px;
        box-shadow: 0 0 10px rgba(0,0,0,0.1);
      }
      h1 {
        color: #0066cc;
        text-align: center;
        border-bottom: 2px solid #f0f0f0;
        padding-bottom: 15px;
        margin-bottom: 25px;
      }
      .flex-container {
        display: flex;
        flex-wrap: wrap;
        gap: 20px;
      }
      .status-box, .visualization-box {
        flex: 1;
        min-width: 300px;
        border: 1px solid #ddd;
        padding: 20px;
        border-radius: 5px;
        margin-bottom: 20px;
        background-color: #f9f9f9;
        border-top: 5px solid #0066cc;
        transition: all 0.3s ease;
      }
      .status-box h2, .visualization-box h2 {
        color: #0066cc;
        margin-top: 0;
        transition: color 0.3s ease;
      }
      .message-type-indicator {
        font-size: 14px;
        text-transform: uppercase;
        letter-spacing: 1px;
        margin-bottom: 5px;
        font-weight: bold;
        transition: color 0.3s ease;
        color: #0066cc;
      }
      .message-box {
        padding: 15px;
        border-radius: 5px;
        margin-bottom: 20px;
        text-align: center;
        font-size: 24px;
        font-weight: bold;
        transition: all 0.3s ease;
        border-left: 5px solid #0066cc;
        background-color: rgba(0, 102, 204, 0.1);
        color: #0066cc;
        box-shadow: 0 2px 5px rgba(0,0,0,0.1);
      }
      .data-container {
        display: none; /* Hide the direction and axis info */
      }
      button {
        background-color: #0066cc;
        color: white;
        border: none;
        padding: 10px 15px;
        border-radius: 5px;
        cursor: pointer;
        width: 100%;
        font-size: 16px;
        margin-bottom: 10px;
        transition: background-color 0.3s ease;
      }
      button:hover {
        background-color: #0052a3;
      }
      #device-model {
        width: 250px;
        height: 250px;
        margin: 0 auto;
        position: relative;
        background-color: #f8f8f8;
        border-radius: 50%;
        border: 1px solid #0066cc;
        transition: border-color 0.3s ease;
      }
      .model-wristband {
        width: 180px;
        height: 40px;
        background-color: #555;
        border-radius: 20px;
        position: absolute;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
        transition: all 0.3s ease;
      }
      .model-sensor {
        width: 60px;
        height: 60px;
        background-color: #0066cc;
        border-radius: 10px;
        position: absolute;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
        box-shadow: 0 0 10px rgba(0,0,0,0.3);
        transition: all 0.3s ease;
      }
      .direction-indicator {
        position: absolute;
        font-weight: bold;
        color: #555;
        background-color: rgba(255, 255, 255, 0.85);
        padding: 3px 8px;
        border-radius: 4px;
        z-index: 10;
        transition: color 0.3s ease, background-color 0.3s ease;
      }
      .north {
        top: 10px;
        left: 50%;
        transform: translateX(-50%);
      }
      .south {
        bottom: 10px;
        left: 50%;
        transform: translateX(-50%);
      }
      .east {
        right: -40px; /* Moved outside the circle */
        top: 50%;
        transform: translateY(-50%);
        font-size: 14px;
      }
      .west {
        left: -40px; /* Moved outside the circle */
        top: 50%;
        transform: translateY(-50%);
        font-size: 14px;
      }
      .axis-line {
        position: absolute;
        background-color: rgba(0,0,0,0.1);
      }
      .x-axis {
        width: 200px;
        height: 2px;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
      }
      .y-axis {
        width: 2px;
        height: 200px;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
      }
      #direction-indicator {
        margin-top: 10px;
        font-weight: bold;
        color: #0066cc;
        transition: color 0.3s ease;
      }
      .log-container {
        height: 200px;
        overflow-y: auto;
        border: 1px solid #ddd;
        padding: 10px;
        border-radius: 5px;
        background-color: white;
      }
      .log-entry {
        margin-bottom: 8px;
        padding: 8px;
        border-bottom: 1px solid #eee;
        transition: background-color 0.3s ease;
      }
      .log-entry:hover {
        background-color: #f9f9f9;
      }
      .log-entry:last-child {
        border-bottom: none;
      }
      /* Animation for message changes - removed */
      .message-box.highlight {
        /* Animation removed as requested */
      }
      /* Device status indicator */
      .device-status {
        display: flex;
        justify-content: space-between;
        margin-bottom: 15px;
        padding-bottom: 15px;
        border-bottom: 1px solid #eee;
      }
      .status-indicator {
        display: inline-block;
        width: 12px;
        height: 12px;
        border-radius: 50%;
        background-color: #4CAF50;
        margin-right: 5px;
        vertical-align: middle;
      }
      .status-text {
        font-size: 14px;
        color: #666;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h1>Motion-Based Message Conveyer</h1>
      
      <div class="device-status">
        <div>
          <span class="status-indicator"></span>
          <span class="status-text">Device Online</span>
        </div>
        <div class="status-text">
          <strong>Last Calibrated:</strong> <span id="last-calibrated">Not calibrated yet</span>
        </div>
      </div>
      
      <div class="flex-container">
        <div class="status-box">
          <h2>Current Status</h2>
          <div id="message-type-indicator" class="message-type-indicator">STATUS NORMAL</div>
          <div class="message-box" id="message-box">Loading...</div>
          <div class="data-container">
            <div class="data-box">
              <h3>Direction</h3>
              <div class="data-value" id="direction-value">--</div>
            </div>
            <div class="data-box">
              <h3>X-Axis</h3>
              <div class="data-value" id="x-value">0.00</div>
            </div>
            <div class="data-box">
              <h3>Y-Axis</h3>
              <div class="data-value" id="y-value">0.00</div>
            </div>
            <div class="data-box">
              <h3>Z-Axis</h3>
              <div class="data-value" id="z-value">0.00</div>
            </div>
          </div>
          <button onclick="refreshData()">Refresh Data</button>
          <button onclick="calibrateSensor()" style="background-color: #ff9800;">Calibrate Device</button>
        </div>
        
        <div class="visualization-box">
          <h2>Device Orientation</h2>
          <div id="device-model">
            <div class="axis-line x-axis"></div>
            <div class="axis-line y-axis"></div>
            <div class="model-wristband" id="wristband"></div>
            <div class="model-sensor" id="sensor"></div>
            <div class="direction-indicator north">North (Front)</div>
            <div class="direction-indicator south">South (Back)</div>
            <div class="direction-indicator east">East (Right)</div>
            <div class="direction-indicator west">West (Left)</div>
          </div>
          <div id="direction-indicator">Flat</div>
        </div>
      </div>
      
      <h2 style="margin-top: 30px; color: #0066cc;">Movement History</h2>
      <div class="log-container" id="log-container">
        <div class="log-entry">Waiting for motion data...</div>
      </div>
    </div>

    <script>
      let autoRefresh = false;
      let refreshInterval;
      // Add flag to track if calibration has been performed
      let calibrationPerformed = false;
      // Use real current time instead of a fixed date
      let lastCalibrationTime = new Date();
      
      function refreshData() {
        fetch('/data')
          .then(response => response.json())
          .then(data => {
            document.getElementById('direction-value').textContent = data.direction;
            document.getElementById('x-value').textContent = data.x.toFixed(2);
            document.getElementById('y-value').textContent = data.y.toFixed(2);
            document.getElementById('z-value').textContent = data.z.toFixed(2);
            
            const messageBox = document.getElementById('message-box');
            messageBox.textContent = data.message;
            
            // Update device orientation visualization
            updateDeviceModel(data.x, data.y, data.z, data.direction);
          })
          .catch(error => console.error('Error fetching data:', error));
          
        // Update history log with color-coded messages
        fetch('/history')
          .then(response => response.json())
          .then(history => {
            const logContainer = document.getElementById('log-container');
            logContainer.innerHTML = '';
            
            // Get the current real time
            const currentTime = new Date();
            
            // Format options
            const options = {
              hour: '2-digit',
              minute: '2-digit',
              second: '2-digit',
              hour12: true
            };
            
            history.forEach((entry, index) => {
              if (entry.direction) {
                // Calculate a timestamp that's a few seconds before now based on index
                // This creates the effect of showing older entries with slightly earlier times
                const entryTime = new Date();
                entryTime.setSeconds(entryTime.getSeconds() - (index * 5)); // Each entry is 5 seconds apart
                
                const timestamp = entryTime.toLocaleTimeString('en-US', options);
                const logEntry = document.createElement('div');
                logEntry.className = 'log-entry';
                
                // Define color coding for different message types - USING THE EXACT COLORS REQUESTED
                let messageColor = "#0066CC"; // Default blue
                let messageType = "STATUS";
                
                // Set color based on message
                if (entry.message.includes("Urgent help needed")) {
                  messageColor = "#FF0000"; // Red
                  messageType = "URGENT";
                } else if (entry.message.includes("Discomfort alert")) {
                  messageColor = "#FFA500"; // Orange
                  messageType = "DISCOMFORT";
                } else if (entry.message.includes("Physical adjustment needed")) {
                  messageColor = "#9370DB"; // Medium Purple
                  messageType = "ADJUSTMENT";
                } else if (entry.message.includes("Water/food needed")) {
                  messageColor = "#2E8B57"; // Sea Green
                  messageType = "NUTRITION";
                }
                
                // Enhanced log entry with color-coded type and message
                logEntry.innerHTML = 
                  `<strong>${timestamp}</strong> - 
                  <span style="color: ${messageColor}; font-weight: bold; background: ${messageColor}10; 
                        padding: 2px 6px; border-radius: 3px;">${messageType}</span>: 
                  <span style="color: ${messageColor}; font-weight: bold;">${entry.message}</span>`;
                
                logContainer.appendChild(logEntry);
              }
            });
          })
          .catch(error => console.error('Error fetching history:', error));
      }
      
      function calibrateSensor() {
        if (confirm('Please ensure the device is in its resting position (flat and stable) before calibrating. Continue?')) {
          // Show a loading message
          const messageBox = document.getElementById('message-box');
          messageBox.textContent = "Calibrating device...";
          messageBox.className = "message-box normal";
          
          fetch('/calibrate')
            .then(response => response.text())
            .then(result => {
              // Update calibration time
              lastCalibrationTime = new Date();
              // Set the flag to true after calibration is performed
              calibrationPerformed = true;
              
              // Format the time for display
              const options = {
                hour: '2-digit',
                minute: '2-digit',
                hour12: true
              };
              document.getElementById('last-calibrated').textContent = 
                `Today at ${lastCalibrationTime.toLocaleTimeString('en-US', options)}`;
              
              // Only show a simple calibration complete message
              alert('Device calibrated successfully!');
              refreshData(); // Refresh data after calibration
            })
            .catch(error => {
              console.error('Error calibrating device:', error);
              alert('Calibration failed. Please try again.');
            });
        }
      }
      
      function updateDeviceModel(x, y, z, direction) {
        const sensor = document.getElementById('sensor');
        const wristband = document.getElementById('wristband');
        const directionIndicator = document.getElementById('direction-indicator');
        const messageBox = document.getElementById('message-box');
        const statusBox = document.querySelector('.status-box');
        const visualizationBox = document.querySelector('.visualization-box');
        const statusBoxHeading = document.querySelector('.status-box h2');
        const visualizationBoxHeading = document.querySelector('.visualization-box h2');
        const deviceModel = document.getElementById('device-model');
        
        // Create or update message type indicator
        let messageTypeIndicator = document.getElementById('message-type-indicator');
        if (!messageTypeIndicator) {
          messageTypeIndicator = document.createElement('div');
          messageTypeIndicator.id = 'message-type-indicator';
          messageTypeIndicator.className = 'message-type-indicator';
          messageBox.parentNode.insertBefore(messageTypeIndicator, messageBox);
        }
        
        // Normalize rotation values (limit to reasonable angles)
        const maxRotation = 70; // Increased maximum rotation angle to 70 degrees
        const xRotation = Math.max(-maxRotation, Math.min(maxRotation, y * 10)); // Y-axis affects X rotation
        const yRotation = Math.max(-maxRotation, Math.min(maxRotation, x * 10)); // X-axis affects Y rotation
        
        // Apply rotation transforms with proper east-west orientation
        const transform = `translate(-50%, -50%) rotateX(${xRotation}deg) rotateY(${yRotation}deg)`;
        sensor.style.transform = transform;
        wristband.style.transform = `translate(-50%, -50%) rotateX(${xRotation}deg) rotateY(${yRotation}deg)`;
        
        // Update direction indicator
        directionIndicator.textContent = direction;
        
        // Reset all compass direction labels
        const northLabel = document.querySelector('.north');
        const southLabel = document.querySelector('.south');
        const eastLabel = document.querySelector('.east');
        const westLabel = document.querySelector('.west');
        
        // Reset all highlights
        northLabel.style.color = '#555';
        southLabel.style.color = '#555';
        eastLabel.style.color = '#555';
        westLabel.style.color = '#555';
        northLabel.style.fontWeight = 'normal';
        southLabel.style.fontWeight = 'normal';
        eastLabel.style.fontWeight = 'normal';
        westLabel.style.fontWeight = 'normal';
        
        // Define message-specific colors - EXACTLY AS REQUESTED
        const urgentColor = "#FF0000";     // Red for "Urgent help needed" (Upward)
        const discomfortColor = "#FFA500";  // Orange for "Discomfort alert" (Downward)
        const adjustmentColor = "#9370DB";  // Medium Purple for "Physical adjustment needed" (Leftward)
        const waterFoodColor = "#2E8B57";   // SeaGreen for "Water/food needed" (Rightward)
        const defaultColor = "#0066CC";     // Default blue for "Normal status" (Flat)
        
        // Apply appropriate color and message type based on direction
        let activeColor = defaultColor;  // Default to blue
        let messageType = "STATUS NORMAL";
        
        // First determine the message based on direction
        if (direction.includes("upward")) {
          activeColor = urgentColor;
          messageType = "URGENT ASSISTANCE";
          northLabel.style.color = urgentColor;
          northLabel.style.fontWeight = "bold";
        } 
        else if (direction.includes("Downward")) {
          activeColor = discomfortColor;
          messageType = "DISCOMFORT";
          southLabel.style.color = discomfortColor;
          southLabel.style.fontWeight = "bold";
        } 
        else if (direction.includes("Leftward")) {
          // Corrected: Leftward motion corresponds to West direction
          activeColor = adjustmentColor;
          messageType = "ADJUSTMENT";
          westLabel.style.color = adjustmentColor; // CHANGED to westLabel
          westLabel.style.fontWeight = "bold";
        } 
        else if (direction.includes("Rightward")) {
          // Corrected: Rightward motion corresponds to East direction
          activeColor = waterFoodColor;
          messageType = "NUTRITION";
          eastLabel.style.color = waterFoodColor; // CHANGED to eastLabel
          eastLabel.style.fontWeight = "bold";
        } 
        else if (direction.includes("Upside down")) {
          activeColor = "#607D8B"; // Blue Gray for device flipped
          messageType = "DEVICE FLIPPED";
          southLabel.style.color = "#607D8B";
          southLabel.style.fontWeight = "bold";
        }
        // else flat position - already defaulted to blue
        
        // Apply coordinated colors across the interface
        
        // 1. Update message box with the active color
        messageBox.style.backgroundColor = `${activeColor}20`; // 20 = 12.5% opacity
        messageBox.style.color = activeColor;
        messageBox.style.borderLeft = `5px solid ${activeColor}`;
        
        // Animation for message changes removed as requested
        
        // 2. Update section boxes borders
        statusBox.style.borderTop = `5px solid ${activeColor}`;
        visualizationBox.style.borderTop = `5px solid ${activeColor}`;
        
        // 3. Update headings
        statusBoxHeading.style.color = activeColor;
        visualizationBoxHeading.style.color = activeColor;
        
        // 4. Update sensor color and direction indicator
        sensor.style.backgroundColor = activeColor;
        directionIndicator.style.color = activeColor;
        
        // 5. Update device model border
        deviceModel.style.borderColor = activeColor;
        
        // 6. Update message type indicator
        messageTypeIndicator.textContent = messageType;
        messageTypeIndicator.style.color = activeColor;
        
        // 7. Update history heading to match
        document.querySelector('h2[style*="margin-top: 30px"]').style.color = activeColor;
      }
      
      // Initial data load
      refreshData();
      
      // Set up auto-refresh every 500 milliseconds for smoother visualization
      setInterval(refreshData, 500);
    </script>
  </body>
  </html>
    )=====";
    
    server.send(200, "text/html", html);
  }

  void handleData() {
    String jsonData = "{";
    jsonData += "\"direction\":\"" + tiltDirection + "\",";
    jsonData += "\"message\":\"" + tiltMessage + "\",";
    jsonData += "\"x\":" + String(x) + ",";
    jsonData += "\"y\":" + String(y) + ",";
    jsonData += "\"z\":" + String(z);
    jsonData += "}";
    
    server.send(200, "application/json", jsonData);
  }

  void handleHistory() {
    String jsonHistory = "[";
    
    for (int i = 0; i < HISTORY_SIZE; i++) {
      int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
      if (motionHistory[idx].timestamp > 0) {
        if (i > 0) jsonHistory += ",";
        jsonHistory += "{";
        jsonHistory += "\"direction\":\"" + motionHistory[idx].direction + "\",";
        jsonHistory += "\"message\":\"" + motionHistory[idx].message + "\",";
        jsonHistory += "\"x\":" + String(motionHistory[idx].x) + ",";
        jsonHistory += "\"y\":" + String(motionHistory[idx].y) + ",";
        jsonHistory += "\"z\":" + String(motionHistory[idx].z) + ",";
        jsonHistory += "\"timestamp\":" + String(motionHistory[idx].timestamp);
        jsonHistory += "}";
      }
    }
    
    jsonHistory += "]";
    server.send(200, "application/json", jsonHistory);
  }

  void handleNotFound() {
    server.send(404, "text/plain", "Page Not Found");
  }