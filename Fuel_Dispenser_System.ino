#include <Wire.h>                  // For I2C communication
#include <LiquidCrystal_I2C.h>      // For the I2C LCD
#include <Keypad.h>                 // For the 4x4 keypad
#include <SPI.h>                    // For RFID
#include <MFRC522.h>                // RFID library

// I2C LCD setup (address 0x27, 20 columns, 4 rows)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Keypad setup
const int ROW_NUM = 4;  // Four rows
const int COLUMN_NUM = 4;  // Four columns

char keys[ROW_NUM][COLUMN_NUM] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte pin_rows[ROW_NUM] = {13, 12, 14, 27};   // Row pinouts
byte pin_column[COLUMN_NUM] = {26, 25, 33, 32}; // Column pinouts

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

// RFID setup
#define SS_PIN 5
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

// Flow sensor setup
const uint8_t FLOW_SENSOR_PIN = 21;    // GPIO pin for flow sensor
const uint8_t RELAY_PIN = 2;           // GPIO pin for relay
const uint16_t PULSES_PER_LITER = 450; // YF-S201 calibration (450 pulses/liter)
const float ML_PER_PULSE = 2.25;       // 2.25mL per pulse for YF-S201
const float PRICE_PER_LITER = 5.0;     // Price per liter (5 Cedis per liter)

volatile uint16_t pulseCount = 0;
volatile float totalMilliLitres = 0;
uint32_t targetMilliLitres = 0;
uint32_t lastUpdateTime = 0;
bool isPumping = false;
bool hasTarget = false;

float amount = 0.0;
String paymentMethod = "";
String mobileNumber = ""; // Store the MoMo number

// ISR for flow sensor pulses
void IRAM_ATTR pulseCounter() {
  pulseCount++;
  // Calculate milliliters in the ISR for more accurate readings
  totalMilliLitres = pulseCount * ML_PER_PULSE;
}


void setup() {
  Serial.begin(115200);  // Initialize serial communication

  Wire.begin(16, 4);  // SDA = GPIO 16, SCL = GPIO 4

  SPI.begin();  // Initialize SPI bus for RFID
  mfrc522.PCD_Init();  // Initialize RFID module
  
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Display the welcome message
  lcd.setCursor(0, 0);
  lcd.print("Smart Fuel Station");
  lcd.setCursor(0, 1);
  lcd.print("Press '#' to start");

  // Wait for '#' key press to start
  while (true) {
    char key = keypad.getKey();  // Get key from the keypad
    
    if (key == '#') {  // If '#' is pressed, proceed to next step
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Starting...");
      delay(1000);  // Wait for a second before starting
      break;  // Exit the loop and proceed to the next part of the program
    }
  }

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, HIGH);  // Pump off

  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);  // Interrupt for flow sensor
}

void loop() {
  enterAmount();
}

void enterAmount() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Amount:");
  lcd.setCursor(0, 1);
  lcd.print("D to delete");

  String inputAmount = ""; // Store the amount input
  int cursorPos = 0;

  while (true) {
    char key = keypad.getKey();
    
    if (key) {
      if (key == '#' && inputAmount.length() > 0) {
        amount = inputAmount.toFloat();
        confirmAmount();
        break;
      }
      else if (key == 'D' && inputAmount.length() > 0) {
        inputAmount.remove(inputAmount.length() - 1);
        cursorPos--;
        lcd.setCursor(0, 2);
        lcd.print("                    ");
        lcd.setCursor(0, 2);
        lcd.print(inputAmount);
      }
      else if (key >= '0' && key <= '9') {
        inputAmount += key;
        cursorPos++;
        lcd.setCursor(0, 2);
        lcd.print(inputAmount);
      }
    }
  }
}

void confirmAmount() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Amount: ");
  lcd.print(amount);
  lcd.print(" C");

  float targetLiters = amount / PRICE_PER_LITER;
  lcd.setCursor(0, 1);
  lcd.print("Target: ");
  lcd.print(targetLiters, 2);
  lcd.print(" L");

  lcd.setCursor(0, 2);
  lcd.print("# to confirm");
  lcd.setCursor(0, 3);
  lcd.print("D to re-enter");

  while (true) {
    char key = keypad.getKey();
    if (key == '#') {
      selectPaymentMethod();
      break;
    } else if (key == 'D') {
      enterAmount();
      break;
    }
  }
}

void selectPaymentMethod() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Select Payment:");
  lcd.setCursor(0, 1);
  lcd.print("A:Card  B:MoMo");
  lcd.setCursor(0, 2);
  lcd.print("D:Back");

  while (true) {
    char key = keypad.getKey();
    
    if (key) {
      if (key == 'A') {
        paymentMethod = "Card";
        promptCardScan();
        break;
      } else if (key == 'B') {
        paymentMethod = "MoMo";
        enterMobileNumber();
        break;
      } else if (key == 'D') {
        confirmAmount();
        break;
      }
    }
  }
}

void enterMobileNumber() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter MoMo No.:");
  lcd.setCursor(0, 1);
  lcd.print("D to delete");

  String inputNumber = ""; // Store the MoMo number input

  while (true) {
    char key = keypad.getKey();

    if (key) {
      if (key == '#' && inputNumber.length() == 10) {
        mobileNumber = inputNumber;
        confirmMobileNumber();
        break;
      }
      else if (key == 'D' && inputNumber.length() > 0) {
        inputNumber.remove(inputNumber.length() - 1);
        lcd.setCursor(0, 2);
        lcd.print("                    ");
        lcd.setCursor(0, 2);
        lcd.print(inputNumber);
      }
      else if (key >= '0' && key <= '9' && inputNumber.length() < 10) {
        inputNumber += key;
        lcd.setCursor(0, 2);
        lcd.print(inputNumber);
      }
    }
  }
}

void confirmMobileNumber() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MoMo: ");
  lcd.print(mobileNumber);
  lcd.setCursor(0, 1);
  lcd.print("# to confirm");
  lcd.setCursor(0, 2);
  lcd.print("D to re-enter");

  while (true) {
    char key = keypad.getKey();
    if (key == '#') {
      processMoMoPayment();
      break;
    } else if (key == 'D') {
      enterMobileNumber();
      break;
    }
  }
}

void processMoMoPayment() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Processing MoMo");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");

  delay(2000); // Simulate payment process

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Payment Success");
  lcd.setCursor(0, 1);
  lcd.print("Ready to dispense");

  delay(2000);
  startDispensing();
}

void promptCardScan() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan your card");
  
  while (!mfrc522.PICC_IsNewCardPresent()) {
  }

  if (mfrc522.PICC_ReadCardSerial()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Card Scanned");
    delay(2000);
    startDispensing();
  }
}


void startDispensing() {
  pulseCount = 0;
  totalMilliLitres = 0;
  targetMilliLitres = (amount / PRICE_PER_LITER) * 1000.0; // Convert amount to volume in mL
  isPumping = true;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Target: ");
  lcd.print(targetMilliLitres / 1000.0, 2);  // Display target in liters
  lcd.print(" L");

  // Start pump
  digitalWrite(RELAY_PIN, LOW); // Turn on relay (start pumping)

  lastUpdateTime = millis();  // Save start time
  float lastDisplayedVolume = 0;  // Track last displayed volume to avoid flicker

  while (totalMilliLitres < targetMilliLitres) {
    // Get the current volume (read volatile variable once)
    float currentVolume = totalMilliLitres;
    
    // Update the display if the volume has changed significantly (0.01L = 10mL)
    if (abs(currentVolume - lastDisplayedVolume) >= 10 || 
        (millis() - lastUpdateTime) >= 200) {  // Update at least every 200ms
        
      // Display current volume
      lcd.setCursor(0, 1);
      lcd.print("Dispensed: ");
      lcd.print(currentVolume / 1000.0, 2);  // Convert to liters
      lcd.print(" L   ");  // Extra spaces to clear old digits
      
      // Display current amount spent
      float amountSpent = (currentVolume / 1000.0) * PRICE_PER_LITER;
      lcd.setCursor(0, 2);
      lcd.print("Amount: ");
      lcd.print(amountSpent, 2);
      lcd.print(" C   ");
      
      // Update tracking variables
      lastDisplayedVolume = currentVolume;
      lastUpdateTime = millis();
      
      // Debug output
      Serial.print("Pulses: ");
      Serial.print(pulseCount);
      Serial.print(" Volume: ");
      Serial.print(currentVolume);
      Serial.println(" mL");
    }

    // Check for emergency stop (optional)
    char key = keypad.getKey();
    if (key == '*') {  // Emergency stop if * is pressed
      break;
    }
  }

  // Stop pump
  digitalWrite(RELAY_PIN, HIGH); // Turn off relay (stop pumping)
  isPumping = false;

  // Final display update
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensing Complete");
  lcd.setCursor(0, 1);
  lcd.print("Volume: ");
  lcd.print(totalMilliLitres / 1000.0, 2);
  lcd.print(" L");
  lcd.setCursor(0, 2);
  lcd.print("Amount: ");
  lcd.print((totalMilliLitres / 1000.0) * PRICE_PER_LITER, 2);
  lcd.print(" C");
  
  delay(3000);  // Show final reading for 3 seconds
}



