#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"
#include <esp_now.h>
#include <WiFi.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LEDS_COUNT  8
#define LEDS_PIN    2
#define CHANNEL     0

#define JOYSTICK_X_PIN 36
#define JOYSTICK_Y_PIN 39
#define SUBMIT_BUTTON_PIN 32
#define BOMB_BUTTON_PIN 13
#define BOMB_INDICATOR_LED_PIN 15
#define BUZZER_PIN 33

int sequence[5];
int playerInput[5];
int sequenceLength = 5;
bool sequenceGenerated = false;
int inputIndex = 0;
int bombSequence = 0;
int score = 0;
bool bombReady = false;
int health = 50;
int patternSpeed = 1000;
const int maxPatternSpeed = 200;
const int speedIncrement = 200;
int pointsPerRound = 1;
bool bombActivated = false;  // Flag to track if the bomb has been activated
bool gameStarted = false;
bool gameOver = false;

bool player1Ready = false;
bool player2Ready = false;

int player1Score = 0;
int player2Score = 0;
int player1Health = 50;
int player2Health = 50;

bool bombButtonPressed = false; 

Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL);

typedef struct struct_message {
  char message[32];
  int player1Score;
  int player2Score;
  int player1Health;
  int player2Health;
} struct_message;

struct_message myData;

int playerRole = 1;  // Set to 1 for Player 1, 2 for Player 2
uint8_t peerAddress[] = {0x10, 0x97, 0xbd, 0xd0, 0xa9, 0xe0};  // Set to the other player's MAC address

// Setup (Pre-game)
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    Serial.println(WiFi.macAddress());

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        for (;;) ;
    }
    display.display();
    delay(2000);
    display.clearDisplay();

    strip.begin();
    strip.setBrightness(50);

    pinMode(JOYSTICK_X_PIN, INPUT);
    pinMode(JOYSTICK_Y_PIN, INPUT);
    pinMode(SUBMIT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BOMB_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BOMB_INDICATOR_LED_PIN, OUTPUT);
    digitalWrite(BOMB_INDICATOR_LED_PIN, LOW);

    updateOLED("Welcome!", "Press to Start");

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    Serial.println("ESP-NOW Initialized");

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
    Serial.println("Peer added");

    esp_now_register_recv_cb(OnDataRecv);
}

// Initial Button Press Screen
void onButtonPress() {
    if (digitalRead(SUBMIT_BUTTON_PIN) == LOW) {
            sendData("Player 1 is ready!");
            player1Ready = true;
            updateOLED("Player 1", "Waiting for Player 2...");
            delay(1000);  // Debounce delay
    }
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    memcpy(&myData, incomingData, sizeof(myData));
    Serial.print("Received message: ");
    Serial.println(myData.message);

    // Print received scores and health for debugging
    Serial.print("Received Player 1 Score: ");
    Serial.println(myData.player1Score);
    Serial.print("Received Player 2 Score: ");
    Serial.println(myData.player2Score);
    Serial.print("Received Player 1 Health: ");
    Serial.println(myData.player1Health);
    Serial.print("Received Player 2 Health: ");
    Serial.println(myData.player2Health);

    // Update local variables based on the role
    if (playerRole == 1) {
        // Player 1 updates based on Player 2's data
        player2Score = myData.player2Score;
        player2Health = myData.player2Health;
    } else if (playerRole == 2) {
        // Player 2 updates based on Player 1's data
        player1Score = myData.player1Score;
        player1Health = myData.player1Health;
    }

    // Update display with the received data
    updateScoresAndHealthDisplay();

    // Small delay to ensure synchronization before processing further
    delay(100);

    if (strcmp(myData.message, "Player 1 is ready!") == 0) {
        player1Ready = true;
        if (playerRole == 2) updateOLED("Player 2", "Player 1 is ready!");
    } else if (strcmp(myData.message, "Player 2 is ready!") == 0) {
        player2Ready = true;
        if (playerRole == 1) updateOLED("Player 1", "Player 2 is ready!");
    }

    // Delay before starting the countdown to ensure both players are ready
    delay(3000);

    if (player1Ready && player2Ready && !gameStarted) {
        startCountdown();
    }

    // Player 1 receives bomb usage
    if (strcmp(myData.message, "Bomb Used") == 0) {
        if (playerRole == 1) {
            // Decrease Player 1's health
            player1Health -= 10;
            if (player1Health < 0) player1Health = 0;

            // Send the updated health back to Player 2
            sendData("Health Updated");

            // Update Player 1's display and show bomb animation
            updateScoresAndHealthDisplay();
            drawBombAnimation();
        }
    }

    // Player 2 receives Player 1's updated health
    if (strcmp(myData.message, "Health Updated") == 0 && playerRole == 2) {
        // Player 2 updates Player 1's health on their screen
        player1Health = myData.player1Health;
        updateScoresAndHealthDisplay();
    }

    if (strcmp(myData.message, "Game Over Player2") == 0) {
        // If Player 2 loses, Player 1 will show victory
        if (playerRole == 1) {
            display.clearDisplay(); 
            updateOLED("You win!", "Player 2 Defeated");
            gameOver = true;
            soundBuzzer();
            resetGame();
        }
    }

    if (strcmp(myData.message, "Game Over Tie") == 0) {
        // Both players show a tie
        display.clearDisplay(); 
        updateOLED("It's a tie!", "Press to Restart");
        gameOver = true;
        soundBuzzer();
        resetGame();
    }

    // Update the display with the received data
    if (!gameOver){
    updateScoresAndHealthDisplay();
    }
}


void sendData(const char* message) {
    strcpy(myData.message, message);

    // Include Player 1 or Player 2's updated health depending on role
    if (playerRole == 1) {
        myData.player1Score = player1Score;
        myData.player1Health = player1Health;
    } else if (playerRole == 2) {
        myData.player2Score = player2Score;
        myData.player2Health = player2Health;  // Send updated health back to Player 1
    }

    esp_err_t result = esp_now_send(NULL, (uint8_t *) &myData, sizeof(myData));
    if (result == ESP_OK) {
        Serial.println("Sent with success");
    } else {
        Serial.println("Error sending the data");
    }

    bombActivated = false;  // Reset after sending
}




// Update Scores and Health Display
void updateScoresAndHealthDisplay() {

    if (gameOver) {
        return;  // Do not update the scoreboard if the game is over
    }

    display.clearDisplay();

    // Set text size and color for the labels "You" and "Player 2"
    display.setTextSize(1); // Increased size for labels
    display.setTextColor(SSD1306_WHITE);

    // Display "You" label
    display.setCursor(0, 0); // Adjusted to start at the top-left corner
    display.println("You");

    // Display "Player 2" label
    display.setCursor(64, 0); // Adjusted to start near the middle of the screen
    display.println("Player 2");

    // Set text size back to 1 for health and score values
    display.setTextSize(1); 

    // Display Player 1 (You) health and score
    display.setCursor(0, 25); // Adjusted Y position to move it down
    display.print("Health: ");
    display.println(player1Health);
    display.setCursor(0, 35); // Adjusted Y position to move it down
    display.print("Score: ");
    display.println(player1Score);

    // Display Player 2 health and score
    display.setCursor(64, 25); // Adjusted Y position to move it down
    display.print("Health: ");
    display.println(player2Health);
    display.setCursor(64, 35); // Adjusted Y position to move it down
    display.print("Score: ");
    display.println(player2Score);

    display.display();
}


// Start Countdown
void startCountdown() {
    updateOLED("Connecting...", "");
    delay(1000);

    for (int i = 3; i > 0; i--) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.println(i);
        display.display();
        delay(1000);
    }
    updateOLED("Go!", "");
    delay(1000);

    gameStarted = true;
    sequenceGenerated = false;
}

// Main Game Loop
void loop() {
    if (!gameStarted && digitalRead(SUBMIT_BUTTON_PIN) == LOW) {
        onButtonPress();
        while (!(player1Ready && player2Ready)) {
            delay(100);  // Wait for both players to be ready
        }
    }

    if (gameStarted && !gameOver) {
        if (!sequenceGenerated) {
            generateSequence();
            displaySequence();
            sequenceGenerated = true;
        }

        capturePlayerInput();

        // Check the player's input when the submit button is pressed
        if (digitalRead(SUBMIT_BUTTON_PIN) == LOW) {
            // If the input is incomplete or longer than the sequence, mark it as incorrect
            if (inputIndex != sequenceLength) {
                Serial.println("Incorrect sequence!");
                lightUpRing(255, 0, 0);
                player1Health -= 5;
                updateOLED("Health", String(player1Health));
                bombSequence = 0;
                bombReady = false;
                patternSpeed = 1000;
                pointsPerRound = 1;
                digitalWrite(BOMB_INDICATOR_LED_PIN, LOW);
                delay(500); // Debounce delay to prevent multiple triggers
                inputIndex = 0;
                sequenceGenerated = false;
                sendData("Sequence Submitted");
                updateScoresAndHealthDisplay();
            } else {
                checkPlayerInput();  // Call checkPlayerInput if the sequence is complete and correct length
                sendData("Sequence Submitted");
                updateScoresAndHealthDisplay();
                delay(500); // Debounce delay to prevent multiple triggers
            }

            // Check victory conditions after the input is submitted
            checkVictoryConditions();
        }

          // Check bomb button and handle debouncing with state management
        if (digitalRead(BOMB_BUTTON_PIN) == LOW && !bombButtonPressed && bombReady) {
            // If the button is pressed and bomb is ready, activate the bomb
            bombButtonPressed = true;  // Set the flag to indicate that the button has been pressed
            useBomb();  // Trigger the bomb action
        } 

        // If the bomb button is released, reset the bombButtonPressed flag
        if (digitalRead(BOMB_BUTTON_PIN) == HIGH) {
            bombButtonPressed = false;
        }

        // Continuously check for victory conditions
        checkVictoryConditions();
    }

    if (gameOver && digitalRead(SUBMIT_BUTTON_PIN) == LOW) {
        resetGame();
    }
}

// Generate Random Sequence
void generateSequence() {
 for (int i = 0; i < sequenceLength; i++) {
   sequence[i] = random(0, LEDS_COUNT);
   Serial.print("Generated LED ");
   Serial.println(sequence[i]);
 }
}

// Display the Sequence
void displaySequence() {
 for (int i = 0; i < sequenceLength; i++) {
   int currentLED = sequence[i];
   strip.setLedColorData(currentLED, 255, 255, 255);
   strip.show();
   delay(patternSpeed);

   strip.setAllLedsColor(0, 0, 0);
   strip.show();
   delay(500);
 }
}

// Capture Player Input
void capturePlayerInput() {
 if (inputIndex < sequenceLength) {
   int xValue = analogRead(JOYSTICK_X_PIN);
   int yValue = analogRead(JOYSTICK_Y_PIN);

   int joystickPosition = mapJoystickToLed(xValue, yValue);

   if (joystickPosition != -1) {
     playerInput[inputIndex] = joystickPosition;
     strip.setLedColorData(joystickPosition, 0, 0, 255);
     strip.show();
     delay(1000);
     strip.setAllLedsColor(0, 0, 0);
     strip.show();

     inputIndex++;
     if (inputIndex >= sequenceLength) {
       Serial.println("Player input complete.");
     }
   }
 }
}

// Check Player Input
void checkPlayerInput() {
 bool correct = true;
 for (int i = 0; i < sequenceLength; i++) {
   if (playerInput[i] != sequence[i]) {
     correct = false;
     break;
   }
 }

 if (correct) {
   Serial.println("Correct sequence!");
   player1Score += pointsPerRound;
   updateOLED("Score", String(player1Score));
   lightUpRing(0, 255, 0);
   bombSequence++;
   if (bombSequence >= 2) {
     bombReady = true;
     updateOLED("Bomb Ready!", "");
     delay(2000);  // Keep the message on the screen for 2 seconds
     digitalWrite(BOMB_INDICATOR_LED_PIN, HIGH);
   }

   patternSpeed = max(patternSpeed - speedIncrement, maxPatternSpeed);
   pointsPerRound = 6 - (patternSpeed / 200);
 } else {
   Serial.println("Incorrect sequence! Try again.");
   player1Health -= 5;
   updateOLED("Health", String(player1Health));
   bombSequence = 0;
   bombReady = false;
   patternSpeed = 1000;
   pointsPerRound = 1;
   lightUpRing(255, 0, 0);
   digitalWrite(BOMB_INDICATOR_LED_PIN, LOW);
 }

 inputIndex = 0;
 sequenceGenerated = false;
}

void useBomb() {
    Serial.println("Bomb used!");

    // Send a message indicating the bomb was used
    sendData("Bomb Used");

    bombReady = false;

    // Update scores and health on the activating player's screen
    updateScoresAndHealthDisplay();

    // Check victory conditions after the bomb use
    checkVictoryConditions();
}



// Bomb Animation
void drawBombAnimation() {
    // Bomb dropping animation
    for (int y = 0; y < SCREEN_HEIGHT; y += 2) { // Adjust the step size for smoothness
        display.clearDisplay();

        // Draw the bomb (a simple filled circle to represent the bomb)
        display.fillCircle(SCREEN_WIDTH / 2, y, 4, SSD1306_WHITE); // Bomb represented by a filled circle

        // Update the display with the bomb's new position
        display.display();

        delay(100); // Adjust the delay for animation speed
    }

    // Explosion animation after the bomb reaches the bottom
    for (int i = 0; i < 10; i++) { // Number of explosion "frames"
        display.clearDisplay();

        // Draw random pixels around the bottom of the screen to simulate an explosion
        for (int j = 0; j < 20; j++) { // Number of explosion particles
            int x = random(SCREEN_WIDTH / 2 - 10, SCREEN_WIDTH / 2 + 10);
            int y = random(SCREEN_HEIGHT - 20, SCREEN_HEIGHT);
            display.drawPixel(x, y, SSD1306_WHITE);
        }

        // Update the display with the explosion effect
        display.display();

        delay(100); // Adjust the delay for explosion speed
    }

    // Clear the screen after the explosion
    display.clearDisplay();
    display.display();
}

// Light Up the Ring
void lightUpRing(int r, int g, int b) {
 strip.setAllLedsColor(r, g, b);
 strip.show();
 delay(1000);
 strip.setAllLedsColor(0, 0, 0);
 strip.show();
}

// Check Victory Conditions
void checkVictoryConditions() {
    if (player1Health <= 0 && player2Health <= 0) {
        // Send game-over message to both players
        sendData("Game Over Tie");
        display.clearDisplay();  // Clear the screen first
        updateOLED("It's a tie!", "Press to Restart");
        gameOver = true;
    } else if (player1Health <= 0) {
        // Player 1 has lost, send game over to Player 2
        sendData("Game Over Player1");
        display.clearDisplay();  // Clear the screen first
        updateOLED("You lose!", "Player 2 Wins");
        gameOver = true;
    } else if (player2Health <= 0) {
        // Player 2 has lost, send game over to Player 1
        sendData("Game Over Player2");
        display.clearDisplay();  // Clear the screen first
        updateOLED("You win!", "Player 2 Defeated");
        gameOver = true;
    }

    if (gameOver) {
        soundBuzzer();
    }
}



// Sound the Buzzer
void soundBuzzer() {
 tone(BUZZER_PIN, 1000);
 delay(1000);
 noTone(BUZZER_PIN);
}

// Reset Game
void resetGame() {
 Serial.println("Resetting game...");
 player1Health = 50;
 player1Score = 0;
 bombSequence = 0;
 bombReady = false;
 patternSpeed = 1000;
 pointsPerRound = 1;
 sequenceGenerated = false;
 gameOver = false;
 digitalWrite(BOMB_INDICATOR_LED_PIN, LOW);
 digitalWrite(BUZZER_PIN, LOW);
 updateOLED("Game Reset", "Press to Start");
}

// Update OLED Display
void updateOLED(String line1, String line2) {
 display.clearDisplay();
 display.setTextSize(2);
 display.setTextColor(SSD1306_WHITE);
 display.setCursor(0, 0);
 display.println(line1);
 display.setTextSize(1);
 display.setCursor(0, 30);
 display.println(line2);
 display.display();
}

// Map Joystick to LED
int mapJoystickToLed(int xValue, int yValue) {
 if (xValue > 4000 && yValue > 1800 && yValue < 2000) return 2;
 if (xValue < 100 && yValue > 1800 && yValue < 2000) return 6;
 if (xValue > 1800 && xValue < 2000 && yValue > 4000) return 4;
 if (xValue > 1800 && xValue < 2000 && yValue < 100) return 0;
 if (xValue > 4000 && yValue > 4000) return 3;
 if (xValue < 100 && yValue > 4000) return 5;
 if (xValue > 4000 && yValue < 100) return 1;
 if (xValue < 100 && yValue < 100) return 7;

 return -1;
}
