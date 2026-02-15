#include <Arduino.h>
#include <unity.h>

// Declare externs from main.cpp to access and manipulate state for tests
extern volatile uint32_t pulseCount;
extern volatile uint64_t lastPulseMicros;
extern volatile uint64_t lastIntervalMicros;
extern float currentCadenceRPM;
extern float currentSpeedKmh;
extern float currentPowerW;
extern uint32_t lastPulseMs;

void updateMetrics();
void sendIndoorBikeData();
void sendCSCMeasurement();
void sendCyclingPowerMeasurement();

// Since NimBLECharacteristic pointers are internal to main.cpp, we
// cannot directly assert on the payloads without exposing hooks.
// For now, we focus on pure logic in updateMetrics by simulating timing.

// Helper to reset state before each test
static void reset_state() {
    pulseCount = 0;
    lastPulseMicros = 0;
    lastIntervalMicros = 0;
    currentCadenceRPM = 0;
    currentSpeedKmh = 0;
    currentPowerW = 0;
    lastPulseMs = 0;
}

// Fake millis() and esp_timer_get_time are not directly stub-able here without
// link-time tricks; therefore we exercise updateMetrics() behavior that does
// not require strict wall-clock control: inactivity timeout and saturation.

void test_inactivity_sets_zero() {
    reset_state();
    // simulate last pulse long ago
    lastPulseMs = millis() - 10000; // > INACTIVITY_TIMEOUT_MS
    lastIntervalMicros = 2000000;   // 2s (ignored due to inactivity)

    // update twice to pass internal 500ms gating
    uint32_t start = millis();
    while (millis() - start < 600) {}
    updateMetrics();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentCadenceRPM);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentPowerW);
}

void test_large_interval_calculates_reasonable_values() {
    reset_state();
    // Simulate a pulse 1 second interval
    lastPulseMs = millis();
    lastIntervalMicros = 1000000ULL; // 1s per rev

    // wait >500ms due to internal gating
    uint32_t start = millis();
    while (millis() - start < 600) {}
    updateMetrics();

    // 1 rev per second -> 60 RPM
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, currentCadenceRPM);
    // Speed = circumference / time * 3.6 = 2.096/1*3.6 = 7.5456 km/h
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 7.55f, currentSpeedKmh);
    // Power ~ 0.02 * v^3
    float expectedP = 0.02f * powf(currentSpeedKmh, 3);
    // updateMetrics already limits to 400W but should be far below
    TEST_ASSERT_FLOAT_WITHIN(1.0f, expectedP, currentPowerW);
}

void test_sanity_limits_cap_values() {
    reset_state();
    lastPulseMs = millis();
    // Unrealistically small interval -> huge speed/power
    lastIntervalMicros = 1000ULL; // 0.001s per rev -> very high

    // wait >500ms due to internal gating
    uint32_t start = millis();
    while (millis() - start < 600) {}
    updateMetrics();

    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(200.0f, currentCadenceRPM);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(100.0f, currentSpeedKmh);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(400.0f, currentPowerW);
}

void test_multiple_pulses_reset_count() {
    reset_state();
    lastPulseMs = millis();
    pulseCount = 5;              // simulate accumulated pulses
    lastIntervalMicros = 500000; // last interval 0.5s

    // wait >500ms due to internal gating
    uint32_t start = millis();
    while (millis() - start < 600) {}
    updateMetrics();

    // After update, pulseCount should be consumed (reset to 0)
    TEST_ASSERT_EQUAL_UINT32(0, pulseCount);
}

void test_zero_interval_keeps_previous_or_zero() {
    reset_state();
    lastPulseMs = millis();
    lastIntervalMicros = 0; // no new interval captured

    // wait >500ms due to internal gating
    uint32_t start = millis();
    while (millis() - start < 600) {}
    updateMetrics();

    // With no interval and not inactive, values should remain at 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentCadenceRPM);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, currentPowerW);
}

void setUp() {}
void tearDown() {}

void setup() {
    delay(2000); // Give time for serial monitor if attached
    UNITY_BEGIN();
    RUN_TEST(test_inactivity_sets_zero);
    RUN_TEST(test_large_interval_calculates_reasonable_values);
    RUN_TEST(test_sanity_limits_cap_values);
    RUN_TEST(test_multiple_pulses_reset_count);
    RUN_TEST(test_zero_interval_keeps_previous_or_zero);
    UNITY_END();
}

void loop() {}
