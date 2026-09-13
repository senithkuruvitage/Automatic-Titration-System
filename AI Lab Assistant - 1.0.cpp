/* 
 * AI-POWERED AUTOMATIC TITRATION LAB ASSISTANT
 *
 * Controller : ESP32-S3 DevKit N16R16
 * Microphone : INMP441
 * Amplifier  : MAX98357A
 * Sensor     : Analog pH sensor with BNC module
 * AI Library : DAZI-AI
 *
 * Features:
 * 1. Reads the pH sensor continuously
 * 2. Tracks titrant volume
 * 3. Detects a possible equivalence point
 * 4. Accepts voice questions
 * 5. Sends live titration data with the question
 * 6. Plays the AI response through a speaker
 */

#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include "Audio.h"

// =====================================================
// USER CONFIGURATION
// =====================================================

// Wi-Fi
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ByteDance / Volcengine ASR
const char *ASR_API_KEY = "YOUR_ASR_API_KEY";
const char *ASR_CLUSTER = "volcengine_input_en";

// OpenAI-compatible API
const char *OPENAI_API_KEY = "YOUR_OPENAI_API_KEY";
const char *OPENAI_BASE_URL = "https://api.openai.com/v1";

// Voice options include:
// alloy, echo, fable, onyx, nova, shimmer
const char *TTS_VOICE = "nova";

// =====================================================
// PIN CONFIGURATION
// =====================================================

// INMP441 microphone
#define MIC_SCK_PIN 5
#define MIC_WS_PIN 4
#define MIC_SD_PIN 6

// MAX98357A amplifier
#define SPEAKER_DOUT_PIN 47
#define SPEAKER_BCLK_PIN 48
#define SPEAKER_LRC_PIN 45

// pH sensor analog output
#define PH_SENSOR_PIN 1

// Button for starting/stopping the assistant
#define ASSISTANT_BUTTON_PIN 0

// Optional pump-step input
// Connect this to the pump control logic or call
// registerDispensedVolume() from your pump code.
#define PUMP_STEP_PIN 14

// =====================================================
// PH SENSOR CALIBRATION
// =====================================================

/*
 * These values are examples only.
 *
 * You must calibrate your own pH sensor using:
 * pH 4.00 buffer
 * pH 7.00 buffer
 * pH 10.00 buffer
 */

// ADC reference and resolution
const float ADC_REFERENCE_VOLTAGE = 3.3;
const int ADC_MAX_VALUE = 4095;

// Example two-point calibration readings
float PH7_VOLTAGE = 2.50;
float PH4_VOLTAGE = 3.00;

// Number of ADC samples used for averaging
const int PH_SAMPLE_COUNT = 20;

// =====================================================
// TITRATION SETTINGS
// =====================================================

// Volume dispensed by one registered pump step
float volumePerPumpStepMl = 0.001;

// Total titrant volume
float totalVolumeMl = 0.0;

// Set according to experiment
String titrationType = "strong acid titrated with strong base";

// Expected equivalence-point pH
float expectedEquivalencePH = 7.0;

// Allowed range around expected equivalence pH
float equivalenceTolerance = 0.15;

// Minimum pH change per added volume considered steep
float minimumSlopeForEquivalence = 1.5;

// =====================================================
// EXPERIMENT DATA
// =====================================================

float currentPH = 7.0;
float previousPH = 7.0;

float previousVolumeMl = 0.0;
float phSlope = 0.0;

float equivalenceVolumeMl = 0.0;
float equivalencePH = 0.0;

bool equivalencePointDetected = false;

unsigned long titrationStartTime = 0;
unsigned long lastPHReadTime = 0;
unsigned long lastStatusPrintTime = 0;

const unsigned long PH_READ_INTERVAL = 500;
const unsigned long STATUS_PRINT_INTERVAL = 2000;

// =====================================================
// AI SYSTEM PROMPT
// =====================================================

const char *SYSTEM_PROMPT =
    "You are an AI laboratory assistant for an automatic acid-base "
    "titration system. "
    "Answer questions using the live experiment data included in the "
    "user message. "
    "Explain pH, titrant volume, equivalence point, endpoint, chemical "
    "changes, sensor behaviour and experimental safety. "
    "Keep spoken answers clear and concise, normally below 70 words. "
    "Never claim that an equivalence point has been confirmed unless "
    "the experiment data says it has been detected. "
    "Do not directly command pumps, motors or actuators. "
    "The embedded control algorithm, not the language model, controls "
    "the experiment. "
    "When data is insufficient, state that clearly. "
    "Warn the user if the pH changes abnormally or if sensor calibration "
    "may be required.";

// =====================================================
// DAZI-AI OBJECTS
// =====================================================

Audio audio;

ArduinoASRChat asrChat(
    ASR_API_KEY,
    ASR_CLUSTER
);

ArduinoGPTChat gptChat(
    OPENAI_API_KEY,
    OPENAI_BASE_URL
);

// =====================================================
// CONVERSATION STATE MACHINE
// =====================================================

enum ConversationState
{
    STATE_IDLE,
    STATE_LISTENING,
    STATE_PROCESSING,
    STATE_PLAYING,
    STATE_WAITING_FOR_AUDIO
};

ConversationState conversationState = STATE_IDLE;

bool continuousConversation = false;
bool currentButtonState = false;
bool previousButtonState = false;

unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;

// =====================================================
// AUDIO CALLBACK
// =====================================================

void audio_eof_speech(const char *info)
{
    Serial.println("[TTS] Audio playback callback received.");
}

// =====================================================
// PH SENSOR FUNCTIONS
// =====================================================

float readPHVoltage()
{
    long adcTotal = 0;

    for (int i = 0; i < PH_SAMPLE_COUNT; i++)
    {
        adcTotal += analogRead(PH_SENSOR_PIN);
        delay(2);
    }

    float averageADC =
        (float)adcTotal / (float)PH_SAMPLE_COUNT;

    return averageADC *
           ADC_REFERENCE_VOLTAGE /
           ADC_MAX_VALUE;
}

float convertVoltageToPH(float voltage)
{
    /*
     * Two-point linear calibration:
     *
     * Point 1: PH7_VOLTAGE represents pH 7
     * Point 2: PH4_VOLTAGE represents pH 4
     */

    float voltageDifference =
        PH4_VOLTAGE - PH7_VOLTAGE;

    if (fabs(voltageDifference) < 0.001)
    {
        Serial.println(
            "[ERROR] Invalid pH calibration values."
        );

        return 7.0;
    }

    float slope =
        (4.0 - 7.0) / voltageDifference;

    float calculatedPH =
        7.0 +
        slope * (voltage - PH7_VOLTAGE);

    return constrain(calculatedPH, 0.0, 14.0);
}

void updatePHSensor()
{
    if (millis() - lastPHReadTime < PH_READ_INTERVAL)
    {
        return;
    }

    lastPHReadTime = millis();

    float voltage = readPHVoltage();
    currentPH = convertVoltageToPH(voltage);
}

// =====================================================
// PUMP AND VOLUME FUNCTIONS
// =====================================================

void registerDispensedVolume(float volumeMl)
{
    if (volumeMl <= 0.0)
    {
        return;
    }

    previousVolumeMl = totalVolumeMl;
    previousPH = currentPH;

    totalVolumeMl += volumeMl;

    delay(150);

    updatePHSensor();
    calculatePHSlope();
    detectEquivalencePoint();
}

void registerPumpSteps(long numberOfSteps)
{
    if (numberOfSteps <= 0)
    {
        return;
    }

    float dispensedVolume =
        numberOfSteps * volumePerPumpStepMl;

    registerDispensedVolume(dispensedVolume);
}

void calculatePHSlope()
{
    float volumeChange =
        totalVolumeMl - previousVolumeMl;

    float phChange =
        currentPH - previousPH;

    if (fabs(volumeChange) > 0.0001)
    {
        phSlope = phChange / volumeChange;
    }
    else
    {
        phSlope = 0.0;
    }
}

// =====================================================
// EQUIVALENCE-POINT DETECTION
// =====================================================

void detectEquivalencePoint()
{
    if (equivalencePointDetected)
    {
        return;
    }

    bool phNearExpectedValue =
        fabs(currentPH - expectedEquivalencePH)
        <= equivalenceTolerance;

    bool steepPHChange =
        fabs(phSlope)
        >= minimumSlopeForEquivalence;

    if (phNearExpectedValue && steepPHChange)
    {
        equivalencePointDetected = true;
        equivalenceVolumeMl = totalVolumeMl;
        equivalencePH = currentPH;

        Serial.println();
        Serial.println(
            "======================================"
        );
        Serial.println(
            "POSSIBLE EQUIVALENCE POINT DETECTED"
        );
        Serial.printf(
            "pH: %.3f\n",
            equivalencePH
        );
        Serial.printf(
            "Volume: %.3f mL\n",
            equivalenceVolumeMl
        );
        Serial.printf(
            "dpH/dV: %.3f pH/mL\n",
            phSlope
        );
        Serial.println(
            "======================================"
        );
    }
}

// =====================================================
// EXPERIMENT STATUS
// =====================================================

String getExperimentDuration()
{
    unsigned long elapsedSeconds =
        (millis() - titrationStartTime) / 1000;

    unsigned long minutes =
        elapsedSeconds / 60;

    unsigned long seconds =
        elapsedSeconds % 60;

    return String(minutes) +
           " minutes " +
           String(seconds) +
           " seconds";
}

String getEquivalenceStatus()
{
    if (equivalencePointDetected)
    {
        return "Possible equivalence point detected";
    }

    return "Equivalence point not detected";
}

String buildExperimentContext(
    const String &studentQuestion
)
{
    String prompt;

    prompt.reserve(1000);

    prompt +=
        "LIVE AUTOMATIC TITRATION DATA\n";

    prompt +=
        "--------------------------------\n";

    prompt += "Titration type: ";
    prompt += titrationType;
    prompt += "\n";

    prompt += "Current pH: ";
    prompt += String(currentPH, 3);
    prompt += "\n";

    prompt += "Previous pH: ";
    prompt += String(previousPH, 3);
    prompt += "\n";

    prompt += "Total titrant added: ";
    prompt += String(totalVolumeMl, 3);
    prompt += " mL\n";

    prompt += "Current pH slope, dpH/dV: ";
    prompt += String(phSlope, 3);
    prompt += " pH per mL\n";

    prompt += "Expected equivalence pH: ";
    prompt += String(expectedEquivalencePH, 2);
    prompt += "\n";

    prompt += "Equivalence status: ";
    prompt += getEquivalenceStatus();
    prompt += "\n";

    if (equivalencePointDetected)
    {
        prompt += "Detected equivalence pH: ";
        prompt += String(equivalencePH, 3);
        prompt += "\n";

        prompt += "Detected equivalence volume: ";
        prompt += String(equivalenceVolumeMl, 3);
        prompt += " mL\n";
    }

    prompt += "Experiment duration: ";
    prompt += getExperimentDuration();
    prompt += "\n";

    prompt +=
        "--------------------------------\n";

    prompt += "Student question: ";
    prompt += studentQuestion;
    prompt += "\n";

    prompt +=
        "Answer based on the supplied live data.";

    return prompt;
}

void printExperimentStatus()
{
    if (
        millis() - lastStatusPrintTime <
        STATUS_PRINT_INTERVAL
    )
    {
        return;
    }

    lastStatusPrintTime = millis();

    Serial.println();
    Serial.println("------ TITRATION STATUS ------");

    Serial.printf(
        "Current pH       : %.3f\n",
        currentPH
    );

    Serial.printf(
        "Titrant volume   : %.3f mL\n",
        totalVolumeMl
    );

    Serial.printf(
        "pH slope         : %.3f pH/mL\n",
        phSlope
    );

    Serial.print("Equivalence      : ");
    Serial.println(getEquivalenceStatus());

    Serial.println("------------------------------");
}

// =====================================================
// AI ASSISTANT FUNCTIONS
// =====================================================

void startListening()
{
    conversationState = STATE_LISTENING;

    if (asrChat.startRecording())
    {
        Serial.println();
        Serial.println(
            "[ASR] Listening. Ask a question."
        );
    }
    else
    {
        Serial.println(
            "[ERROR] Could not start ASR."
        );

        continuousConversation = false;
        conversationState = STATE_IDLE;
    }
}

void startContinuousConversation()
{
    continuousConversation = true;

    Serial.println();
    Serial.println(
        "======================================"
    );
    Serial.println(
        "AI LAB ASSISTANT STARTED"
    );
    Serial.println(
        "Press BOOT again to stop"
    );
    Serial.println(
        "======================================"
    );

    startListening();
}

void stopContinuousConversation()
{
    continuousConversation = false;

    if (asrChat.isRecording())
    {
        asrChat.stopRecording();
    }

    conversationState = STATE_IDLE;

    Serial.println();
    Serial.println(
        "AI lab assistant stopped."
    );
}

void restartListening()
{
    if (!continuousConversation)
    {
        conversationState = STATE_IDLE;
        return;
    }

    delay(500);
    startListening();
}

void processRecognizedQuestion()
{
    String recognizedText =
        asrChat.getRecognizedText();

    asrChat.clearResult();

    if (recognizedText.length() == 0)
    {
        Serial.println(
            "[ASR] No speech was recognized."
        );

        restartListening();
        return;
    }

    Serial.println();
    Serial.println(
        "===== STUDENT QUESTION ====="
    );
    Serial.println(recognizedText);
    Serial.println(
        "============================"
    );

    conversationState = STATE_PROCESSING;

    String completePrompt =
        buildExperimentContext(recognizedText);

    Serial.println(
        "[AI] Sending question with live data..."
    );

    String response =
        gptChat.sendMessage(completePrompt);

    if (response.length() == 0)
    {
        Serial.println(
            "[ERROR] AI returned an empty response."
        );

        restartListening();
        return;
    }

    Serial.println();
    Serial.println(
        "===== AI LAB ASSISTANT ====="
    );
    Serial.println(response);
    Serial.println(
        "============================"
    );

    conversationState = STATE_PLAYING;

    bool ttsStarted =
        gptChat.textToSpeech(
            response,
            TTS_VOICE
        );

    if (!ttsStarted)
    {
        Serial.println(
            "[ERROR] Text-to-speech failed."
        );

        restartListening();
        return;
    }

    conversationState =
        STATE_WAITING_FOR_AUDIO;

    ttsStartTime = millis();
    ttsCheckTime = millis();
}

void handleAssistantButton()
{
    currentButtonState =
        digitalRead(ASSISTANT_BUTTON_PIN) == LOW;

    if (
        currentButtonState &&
        !previousButtonState
    )
    {
        previousButtonState = true;

        if (
            !continuousConversation &&
            conversationState == STATE_IDLE
        )
        {
            startContinuousConversation();
        }
        else if (continuousConversation)
        {
            stopContinuousConversation();
        }
    }

    if (
        !currentButtonState &&
        previousButtonState
    )
    {
        previousButtonState = false;
    }
}

// =====================================================
// WIFI
// =====================================================

bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to Wi-Fi");

    int attempts = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 30
    )
    {
        Serial.print(".");
        delay(500);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println();
        Serial.println(
            "[ERROR] Wi-Fi connection failed."
        );

        return false;
    }

    Serial.println();
    Serial.println("Wi-Fi connected.");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    return true;
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println(
        "AUTOMATIC TITRATION AI LAB ASSISTANT"
    );

    pinMode(
        ASSISTANT_BUTTON_PIN,
        INPUT_PULLUP
    );

    pinMode(
        PH_SENSOR_PIN,
        INPUT
    );

    analogReadResolution(12);

    titrationStartTime = millis();

    if (!connectToWiFi())
    {
        return;
    }

    // MAX98357A configuration
    audio.setPinout(
        SPEAKER_BCLK_PIN,
        SPEAKER_LRC_PIN,
        SPEAKER_DOUT_PIN
    );

    audio.setVolume(80);

    // Configure AI
    gptChat.setSystemPrompt(
        SYSTEM_PROMPT
    );

    gptChat.enableMemory(true);

    // Configure INMP441
    bool microphoneReady =
        asrChat.initINMP441Microphone(
            MIC_SCK_PIN,
            MIC_WS_PIN,
            MIC_SD_PIN
        );

    if (!microphoneReady)
    {
        Serial.println(
            "[ERROR] INMP441 initialization failed."
        );

        return;
    }

    asrChat.setAudioParams(
        16000,
        16,
        1
    );

    asrChat.setSilenceDuration(1000);
    asrChat.setMaxRecordingSeconds(30);

    asrChat.setTimeoutNoSpeechCallback(
        []()
        {
            Serial.println(
                "[ASR] No-speech timeout."
            );

            if (continuousConversation)
            {
                stopContinuousConversation();
            }
        }
    );

    if (!asrChat.connectWebSocket())
    {
        Serial.println(
            "[ERROR] ASR service connection failed."
        );

        return;
    }

    // Initial pH reading
    for (int i = 0; i < 10; i++)
    {
        updatePHSensor();
        delay(100);
    }

    previousPH = currentPH;

    Serial.println();
    Serial.println(
        "System ready."
    );

    Serial.println(
        "Press the BOOT button to start speaking."
    );
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
    // Essential DAZI-AI processing
    audio.loop();
    asrChat.loop();

    // Continue reading sensor while AI is operating
    updatePHSensor();
    printExperimentStatus();

    handleAssistantButton();

    switch (conversationState)
    {
        case STATE_IDLE:
            break;

        case STATE_LISTENING:

            if (asrChat.hasNewResult())
            {
                processRecognizedQuestion();
            }

            break;

        case STATE_PROCESSING:
            break;

        case STATE_PLAYING:
            break;

        case STATE_WAITING_FOR_AUDIO:

            if (millis() - ttsCheckTime > 100)
            {
                ttsCheckTime = millis();

                if (!audio.isRunning())
                {
                    Serial.println(
                        "[TTS] Response completed."
                    );

                    restartListening();
                }
                else if (
                    millis() - ttsStartTime >
                    60000
                )
                {
                    Serial.println(
                        "[TTS] Playback timeout."
                    );

                    restartListening();
                }
            }

            break;
    }

    if (
        conversationState ==
        STATE_LISTENING
    )
    {
        yield();
    }
    else
    {
        delay(10);
    }
}
