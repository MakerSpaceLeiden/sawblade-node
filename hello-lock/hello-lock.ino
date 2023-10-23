const int LED_PIN = 5;
const int LOCK_PIN = 4;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(LOCK_PIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  Serial.println("de-activate lock");
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LOCK_PIN, HIGH); // LOCK (or rather, the relay), is active-low

  delay(3000);

  Serial.println("activate lock");
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(LOCK_PIN, LOW); // pull down relay input to activate

  delay(50);

}
